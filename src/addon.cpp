#include "addon.h"
#include "presentation.h"
#include "shuangpin.h"
#include "punctuation_api.h"
#include "fdstream.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/charutils.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#else
#include <fcitx-utils/standardpath.h>
#include <fcntl.h>
#endif
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyindictionary.h>
#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace phono_fcitx {
using namespace fcitx;
namespace {
FCITX_DEFINE_LOG_CATEGORY(contextLog, "phono-context")
auto openUserData(const std::string &name) {
#if __has_include(<fcitx-utils/standardpaths.h>)
    return StandardPaths::global().open(StandardPathsType::PkgData, name, StandardPathsMode::User);
#else
    return StandardPath::global().openUser(StandardPath::Type::PkgData, name, O_RDONLY);
#endif
}
template<typename Callback> bool saveUserData(const std::string &name, Callback &&callback) {
#if __has_include(<fcitx-utils/standardpaths.h>)
    return StandardPaths::global().safeSave(StandardPathsType::PkgData, name, std::forward<Callback>(callback));
#else
    return StandardPath::global().safeSave(StandardPath::Type::PkgData, name, std::forward<Callback>(callback));
#endif
}
class Word : public CandidateWord {
public:
    Word(Engine *engine, uint64_t generation, bool model, size_t index, Text text)
        : CandidateWord(std::move(text)), engine_(engine), generation_(generation),
          model_(model), index_(index) {}
    void select(InputContext *ic) const override { engine_->select(ic, generation_, model_, index_); }
private:
    Engine *engine_;
    uint64_t generation_;
    bool model_;
    size_t index_;
};
std::string tail(const std::string &text, size_t limit) {
    const auto length = utf8::lengthValidated(text);
    if (length == utf8::INVALID_LENGTH) return {};
    if (length <= limit) return text;
    return std::string(utf8::nextNChar(text.begin(), length - limit), text.end());
}
KeyList selectionKeys() {
    return {Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
            Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
            Key(FcitxKey_9), Key(FcitxKey_0)};
}
bool sameText(const Text &a, const Text &b) {
    if (a.empty() && b.empty()) return true;
    if (a.cursor() != b.cursor() || a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a.stringAt(i) != b.stringAt(i) || a.formatAt(i) != b.formatAt(i)) return false;
    return true;
}
bool phonoEntry(const InputMethodEntry *entry) {
    return entry && (entry->uniqueName() == "phono" || entry->uniqueName() == "phono-shuangpin");
}
Text candidateText(const CandidatePresentation &presentation) {
    Text result;
    for (const auto &part : presentation.pieces)
        result.append(part.text, part.deleted ? TextFormatFlags{TextFormatFlag::Strike, TextFormatFlag::DontCommit}
                                             : TextFormatFlags{});
    return result;
}
SurroundingSnapshot surroundingSnapshot(InputContext *ic, size_t maxBytes) {
    const auto &surrounding = ic->surroundingText();
    if (!surrounding.isValid() || surrounding.text().size() > maxBytes) return {};
    return {surrounding.text(), surrounding.cursor(), surrounding.anchor(), surrounding.isValid()};
}
std::optional<std::string> applicationHistory(InputContext *ic, size_t limit) {
    const auto &surrounding = ic->surroundingText();
    if (!ic->capabilityFlags().test(CapabilityFlag::SurroundingText) || !surrounding.isValid()) return {};
    const auto &text = surrounding.text();
    const auto length = utf8::lengthValidated(text);
    const auto position = std::min(surrounding.cursor(), surrounding.anchor());
    if (length == utf8::INVALID_LENGTH || position > length) return {};
    return tail(std::string(text.begin(), utf8::nextNChar(text.begin(), position)), limit);
}
}

Engine::Engine(Instance *instance, Worker::Inference inference)
    : instance_(instance),
      ime_(std::make_unique<libime::PinyinIME>(
          std::make_unique<libime::PinyinDictionary>(),
          std::make_unique<libime::UserLanguageModel>(
              libime::DefaultLanguageModelResolver::instance().languageModelFileForLanguage("zh_CN")))),
      worker_(inference ? std::move(inference) : Worker::Inference([this](const Request &r) { return core_.infer(r); })),
      factory_([this](InputContext &) { return new State(ime_.get(), ++nextContext_); }) {
    dispatcher_.attach(&instance_->eventLoop());
    ime_->dict()->load(libime::PinyinDictionary::SystemDict, PHONO_PINYIN_DICT,
                       libime::PinyinDictFormat::Binary);
    ime_->setNBest(3);
    ime_->setScoreFilter(1);
    ime_->setPreeditMode(libime::PinyinPreeditMode::RawText);
    for (const auto &name : {"phono/user.dict", "phono/user.history"}) {
        auto file = openUserData(name);
        if (!file.isValid()) continue;
        try {
            InputFDStreamBuf buffer(file.fd());
            std::istream in(&buffer);
            if (std::string(name).ends_with("dict"))
                ime_->dict()->load(libime::PinyinDictionary::UserDict, in, libime::PinyinDictFormat::Binary);
            else ime_->model()->load(in);
        } catch (const std::exception &e) { FCITX_WARN() << "Phono: " << e.what(); }
    }
    instance_->inputContextManager().registerProperty("phonoState", &factory_);
    reloadConfig();
    watchers_.push_back(instance_->watchEvent(EventType::InputContextSurroundingTextUpdated,
        EventWatcherPhase::Default, [this](Event &event) {
            auto *ic = static_cast<InputContextEvent &>(event).inputContext();
            const auto *entry = instance_->inputMethodEntry(ic);
            if (phonoEntry(entry)) {
                auto *s = state(ic);
                const auto prefix = *config_.surrounding && !sensitive(ic)
                    ? applicationHistory(ic, *config_.historyChars) : std::nullopt;
                const char *action = "unavailable";
                if (prefix) {
                    const auto &surrounding = ic->surroundingText();
                    const auto movedIn = [&](const std::optional<SurroundingSnapshot> &known) {
                        return known && known->valid && surrounding.text() == known->text &&
                            (surrounding.cursor() != known->cursor || surrounding.anchor() != known->anchor);
                    };
                    const bool movedInCommittedDocument = movedIn(s->pendingSurrounding) || movedIn(s->pendingAcknowledged);
                    if (s->pendingHistory && !movedInCommittedDocument && *prefix != *s->pendingHistory &&
                        std::find(s->pendingPrefixes.begin(), s->pendingPrefixes.end(), *prefix) != s->pendingPrefixes.end()) {
                        action = "partial-or-repeated";
                        s->pendingAcknowledged = surroundingSnapshot(ic, 4 * (size_t(*config_.historyChars) + 4096));
                    } else {
                        action = s->pendingHistory ? "acknowledged-or-edited" : "application";
                        s->clearPendingHistory();
                        s->localHistory = *prefix;
                    }
                }
                // Invalid notifications cannot acknowledge a commit. Keep the
                // last known prefix until usable data or an edit/focus reset.
                FCITX_LOGC(contextLog, Debug) << "surrounding id=" << s->id << " frontend=" << ic->frontend()
                    << " capability=" << ic->capabilityFlags().test(CapabilityFlag::SurroundingText)
                    << " valid=" << ic->surroundingText().isValid()
                    << " cursor=" << ic->surroundingText().cursor() << " anchor=" << ic->surroundingText().anchor()
                    << " action=" << action
                    << " leftChars=" << (prefix ? utf8::length(*prefix) : 0)
                    << " pending=" << s->pendingPrefixes.size();
                update(ic, true);
            }
        }));
    watchers_.push_back(instance_->watchEvent(EventType::InputContextCapabilityChanged,
        EventWatcherPhase::Default, [this](Event &event) {
            auto *ic = static_cast<InputContextEvent &>(event).inputContext();
            if (sensitive(ic)) {
                auto *s = state(ic);
                s->localHistory.clear();
                s->clearPendingHistory();
                s->invalidate();
            }
            const auto *entry = instance_->inputMethodEntry(ic);
            if (phonoEntry(entry)) {
                update(ic, true);
            }
        }));
}
Engine::~Engine() {
    alive_->store(false);
    watchers_.clear();
    worker_.stop();
    // Property destruction precedes PinyinIME destruction.
    factory_.unregister();
}
State *Engine::state(InputContext *ic) { return ic->propertyFor(&factory_); }
bool Engine::sensitive(InputContext *ic) const {
    return ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive);
}
std::string Engine::history(InputContext *ic) const {
    if (sensitive(ic)) return {};
    const auto *s = ic->propertyFor(&factory_);
    if (*config_.surrounding) {
        if (s->pendingHistory) return *s->pendingHistory;
        if (const auto prefix = applicationHistory(ic, *config_.historyChars)) return *prefix;
    }
    return tail(s->localHistory, *config_.historyChars);
}
void Engine::recordCommitHistory(InputContext *ic, const std::string &prior, const std::string &text) {
    auto *s = state(ic);
    if (sensitive(ic)) { s->localHistory.clear(); s->clearPendingHistory(); return; }
    s->localHistory = tail(prior + text, *config_.historyChars);
    if (*config_.surrounding && (s->pendingHistory || applicationHistory(ic, *config_.historyChars))) {
        if (!s->pendingHistory) {
            s->pendingSurrounding = surroundingSnapshot(ic, 4 * (size_t(*config_.historyChars) + 4096));
            s->pendingAcknowledged = s->pendingSurrounding;
            s->pendingPrefixes.push_back(prior);
        }
        auto &expected = *s->pendingSurrounding;
        if (expected.valid) {
            const auto begin = utf8::nextNChar(expected.text.begin(), std::min(expected.cursor, expected.anchor));
            const auto end = utf8::nextNChar(expected.text.begin(), std::max(expected.cursor, expected.anchor));
            const auto position = std::min(expected.cursor, expected.anchor) + utf8::length(text);
            expected.text.replace(begin, end, text);
            expected.cursor = expected.anchor = position;
            // Some frontends never confirm commits. Bound document prediction
            // as well as the prefix queue; full-document cursor detection is
            // optional when the retained window becomes too large.
            if (expected.text.size() > 4 * (size_t(*config_.historyChars) + 4096)) expected = {};
        }
        if (s->pendingPrefixes.back() != s->localHistory) s->pendingPrefixes.push_back(s->localHistory);
        if (s->pendingPrefixes.size() > 64) s->pendingPrefixes.erase(s->pendingPrefixes.begin() + 1);
        s->pendingHistory = s->localHistory;
    }
}
void Engine::render(InputContext *ic) {
    auto *s = state(ic);
    auto &panel = ic->inputPanel();
    Text preedit;
    if (!s->pinyin.empty()) {
        const auto [text, cursor] = s->pinyin.preeditWithCursor(libime::PinyinPreeditMode::RawText);
        preedit = Text(text, TextFormatFlag::Underline);
        preedit.setCursor(cursor);
    }
    const bool preeditChanged = !sameText(panel.clientPreedit(), preedit);
    panel.reset();
    if (!s->pinyin.empty()) {
        panel.setClientPreedit(preedit);
        panel.setPreedit(preedit);
        auto list = std::make_unique<CommonCandidateList>();
        list->setPageSize(*config_.pageSize);
        list->setSelectionKey(selectionKeys());
        std::unordered_set<std::string> seen;
        for (size_t i = 0; i < s->beams.size(); ++i) {
            const auto presentation = presentCandidate(s->beams[i], s->invalid, *config_.invalidInput);
            if (seen.insert(presentation.committed).second)
                list->append<Word>(this, s->generation, true, i, candidateText(presentation));
        }
        const auto &classic = s->pinyin.candidatesToCursor();
        // Model proposals take the leading positions, but cannot erase a
        // dictionary alternative which the model did not produce (e.g. 嘛).
        for (size_t i = 0; i < classic.size(); ++i)
            if (seen.insert(classic[i].toString()).second)
                list->append<Word>(this, s->generation, false, i, Text(classic[i].toString()));
        if (!list->empty()) list->setGlobalCursorIndex(0);
        panel.setCandidateList(std::move(list));
        if (!s->error.empty()) panel.setAuxDown(Text(s->error));
        else if (s->splitting) panel.setAuxDown(Text(_("Dictionary segmentation")));
    }
    // Candidate-only updates must not resend the same composition to the
    // frontend, which may respond with another surrounding-text notification.
    if (preeditChanged) ic->updatePreedit();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void Engine::update(InputContext *ic, bool infer) {
    auto *s = state(ic);
    if (sensitive(ic)) { s->localHistory.clear(); s->clearPendingHistory(); }
    if (s->pinyin.empty()) s->splitting = false;
    const bool eligible = infer && *config_.enabled && !s->pinyin.empty() &&
        !s->splitting && s->pinyin.selectedLength() == 0 &&
        s->pinyin.cursor() == s->pinyin.size() &&
        s->pinyin.size() <= static_cast<size_t>(*config_.maxChars) && !sensitive(ic) && ic->hasFocus();
    UpdateSnapshot snapshot{s->pinyin.userInput(), s->pinyin.selectedSentence(),
                            eligible ? history(ic) : std::string{}, s->pinyin.cursor(),
                            s->pinyin.selectedLength(), s->splitting, eligible};
    // Frontends can repeat surrounding notifications without changing the
    // effective left context. Keep both completed beams and in-flight work,
    // as well as the user's current candidate page and cursor, in that case.
    if (s->lastUpdate && *s->lastUpdate == snapshot) return;
    s->invalidate();
    s->lastUpdate = std::move(snapshot);
    if (eligible && config_.model->empty()) s->error = _("Set the model package directory in Phono settings");
    render(ic);
    if (!eligible || config_.model->empty()) return;
    Request request;
    request.contextId = s->id;
    request.pinyin = s->pinyin.userInput();
    request.history = s->lastUpdate->history;
    FCITX_LOGC(contextLog, Debug) << "request id=" << s->id << " frontend=" << ic->frontend()
        << " source=" << (s->pendingHistory ? "pending" :
            (*config_.surrounding && applicationHistory(ic, *config_.historyChars) ? "application" : "cached-local"))
        << " historyChars=" << utf8::length(request.history);
    request.invalidPolicy = *config_.invalidInput;
    request.settings = settingsFor(config_, s->pinyin.useShuangpin());
    if (s->pinyin.useShuangpin()) {
        request.explicitSyllables = true;
        auto converted = convertShuangpin(request.pinyin, *ime_->shuangpinProfile());
        const auto &native = s->pinyin.candidates();
        if (!native.empty()) preferShuangpinReading(converted, s->pinyin.candidateFullPinyin(native.front()));
        for (const auto &token : converted.tokens)
            request.syllables.push_back({token.begin, token.end, token.alternatives});
        for (const auto &range : converted.invalidSpans)
            request.invalid.push_back({range.begin, range.end, 0,
                request.pinyin.substr(range.begin, range.end - range.begin)});
    }
    s->cancelled = request.cancelled;
    const auto generation = s->generation;
    const auto cancelled = request.cancelled;
    auto reference = ic->watch();
    auto alive = alive_;
    // Only the event dispatcher is accessed here from the worker thread.
    worker_.submit(std::move(request),
        [this, reference, generation, cancelled, alive](Result result) mutable {
            dispatcher_.schedule(
                [this, reference, generation, cancelled, alive, result = std::move(result)]() mutable {
                    if (!alive->load() || cancelled->load() || !reference.isValid()) return;
                    auto *ic = reference.get();
                    auto *s = state(ic);
                    if (s->generation != generation || s->splitting || !ic->hasFocus() || sensitive(ic)) return;
                    s->beams = std::move(result.candidates);
                    s->invalid = std::move(result.invalid);
                    if (!result.error.empty()) {
                        FCITX_WARN() << "Phono: " << result.error;
                        s->error = _("Phono inference failed; using dictionary candidates (see log for details)");
                    }
                    render(ic);
                });
        });
}
void Engine::commit(InputContext *ic, const std::string &text, bool learn) {
    auto *s = state(ic);
    const auto priorHistory = history(ic);
    s->invalidate();
    if (learn && *config_.learning && !sensitive(ic)) s->pinyin.learn();
    recordCommitHistory(ic, priorHistory, text);
    s->pinyin.clear();
    s->splitting = false;
    ic->commitString(text);
    render(ic);
}
void Engine::select(InputContext *ic, uint64_t generation, bool model, size_t index) {
    auto *s = state(ic);
    if (s->generation != generation) return;
    if (model) {
        if (s->splitting || index >= s->beams.size()) return;
        const auto text = presentCandidate(s->beams[index], s->invalid, *config_.invalidInput).committed;
        commit(ic, text, false);
    } else {
        if (index >= s->pinyin.candidatesToCursor().size()) return;
        s->splitting = true;
        s->invalidate();
        s->pinyin.selectCandidatesToCursor(index);
        if (s->pinyin.selected()) commit(ic, s->pinyin.selectedSentence(), true);
        else update(ic, false);
    }
}
void Engine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
    if (event.isRelease()) return;
    auto *ic = event.inputContext();
    auto *s = state(ic);
    const auto normalized = event.key().normalize();
    const auto sym = normalized.sym();
    const auto modifiers = normalized.states();
    const bool tab = sym == FcitxKey_Tab && (!modifiers || modifiers == KeyStates{KeyState::Shift});
    // Older Fcitx normalizers strip Shift from Tab. The raw key preserves its
    // direction and also covers ISO_Left_Tab with or without a Shift flag.
    const bool reverseTab = tab && (event.rawKey().sym() == FcitxKey_ISO_Left_Tab ||
                                   event.rawKey().states().test(KeyState::Shift));
    const bool previousPage = reverseTab || (!modifiers &&
        (sym == FcitxKey_Page_Up || sym == FcitxKey_minus || sym == FcitxKey_underscore));
    const bool nextPage = (tab && !reverseTab) || (!modifiers &&
        (sym == FcitxKey_Page_Down || sym == FcitxKey_plus || sym == FcitxKey_equal));
    if (!s->pinyin.empty() && (previousPage || nextPage)) {
        if (s->cancelled) s->cancelled->store(true);
        if (const auto list = ic->inputPanel().candidateList(); list && list->toPageable()) {
            if (previousPage) list->toPageable()->prev(); else list->toPageable()->next();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
        event.filterAndAccept();
        return;
    }
    if (normalized.states()) {
        // Forwarded application shortcuts can edit text outside this engine.
        s->localHistory.clear();
        s->clearPendingHistory();
        if (s->cancelled) s->cancelled->store(true);
        return;
    }
    if ((sym >= FcitxKey_a && sym <= FcitxKey_z) ||
        (sym == FcitxKey_apostrophe && !s->pinyin.empty()) ||
        (sym == FcitxKey_semicolon && !s->pinyin.empty() && s->pinyin.useShuangpin() &&
         ime_->shuangpinProfile()->validInput().contains(';'))) {
        s->pinyin.type(static_cast<uint32_t>(sym));
        event.filterAndAccept();
        update(ic, true);
        return;
    }
    if (s->pinyin.empty()) {
        if (punctuation(ic, normalized)) event.filterAndAccept();
        else { s->localHistory.clear(); s->clearPendingHistory(); }
        return;
    }
    auto list = ic->inputPanel().candidateList();
    auto choose = [&](int index) {
        if (!list || index < 0 || index >= list->size()) return false;
        list->candidate(index).select(ic);
        return true;
    };
    if (sym >= FcitxKey_1 && sym <= FcitxKey_9) {
        if (choose(sym - FcitxKey_1)) event.filterAndAccept();
        return;
    }
    if (sym == FcitxKey_0) { if (choose(9)) event.filterAndAccept(); return; }
    if (sym == FcitxKey_space) {
        event.filterAndAccept();
        if (!choose(list && list->cursorIndex() >= 0 ? list->cursorIndex() : 0))
            commit(ic, s->pinyin.selectedSentence() + s->pinyin.userInput().substr(s->pinyin.selectedLength()), false);
        return;
    }
    if (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter) {
        commit(ic, s->pinyin.selectedSentence() + s->pinyin.userInput().substr(s->pinyin.selectedLength()), false);
        event.filterAndAccept();
        return;
    }
    if (sym == FcitxKey_Escape) {
        s->invalidate();
        s->pinyin.clear();
        s->splitting = false;
        render(ic);
        event.filterAndAccept();
        return;
    }
    if (sym == FcitxKey_Down || sym == FcitxKey_Up) {
        // Do not reorder a list while the user is navigating it.
        if (s->cancelled) s->cancelled->store(true);
        if (list) {
            if (sym == FcitxKey_Down && list->toCursorMovable()) list->toCursorMovable()->nextCandidate();
            else if (sym == FcitxKey_Up && list->toCursorMovable()) list->toCursorMovable()->prevCandidate();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
        event.filterAndAccept();
        return;
    }
    if (sym == FcitxKey_BackSpace) {
        if (s->pinyin.cursor() == s->pinyin.selectedLength() && s->pinyin.selectedLength()) s->pinyin.cancel();
        else s->pinyin.backspace();
    } else if (sym == FcitxKey_Delete) s->pinyin.del();
    else if (sym == FcitxKey_Left) {
        if (s->pinyin.cursor() == s->pinyin.selectedLength() && s->pinyin.selectedLength()) s->pinyin.cancel();
        else if (s->pinyin.cursor()) s->pinyin.setCursor(s->pinyin.cursor() - 1);
    } else if (sym == FcitxKey_Right) s->pinyin.setCursor(std::min(s->pinyin.size(), s->pinyin.cursor() + 1));
    else if (sym == FcitxKey_Home) s->pinyin.setCursor(s->pinyin.selectedLength());
    else if (sym == FcitxKey_End) s->pinyin.setCursor(s->pinyin.size());
    else {
        // Commit the current default before passing punctuation to the app.
        if (!choose(list && list->cursorIndex() >= 0 ? list->cursorIndex() : 0))
            commit(ic, s->pinyin.selectedSentence() + s->pinyin.userInput().substr(s->pinyin.selectedLength()), false);
        // A partial dictionary selection must not leak this key to the app.
        if (!s->pinyin.empty()) event.filterAndAccept();
        else if (punctuation(ic, normalized)) event.filterAndAccept();
        else { s->localHistory.clear(); s->clearPendingHistory(); }
        return;
    }
    event.filterAndAccept();
    update(ic, true);
}
bool Engine::punctuation(InputContext *ic, const Key &key) {
    if (sensitive(ic) || key.isKeyPad()) return false;
    const auto unicode = Key::keySymToUnicode(key.sym());
    if (!unicode) return false;
    auto *module = instance_->addonManager().addon("punctuation", true);
    if (!module) return false;
    const auto [before, after] = module->call<IPunctuation::pushPunctuationV2>("zh_CN", ic, unicode);
    if (before.empty()) return false;
    auto *s = state(ic);
    const auto prior = history(ic);
    s->invalidate();
    recordCommitHistory(ic, prior, before);
    const auto paired = before + after;
    if (!after.empty() && ic->capabilityFlags().test(CapabilityFlag::CommitStringWithCursor))
        ic->commitStringWithCursor(paired, utf8::length(before));
    else {
        ic->commitString(paired);
        for (size_t i = 0; i < utf8::length(after); ++i) ic->forwardKey(Key(FcitxKey_Left));
    }
    render(ic);
    return true;
}
void Engine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *s = state(ic);
    s->invalidate();
    s->pinyin.clear();
    s->splitting = false;
    s->localHistory.clear();
    s->clearPendingHistory();
    render(ic);
}
void Engine::deactivate(const InputMethodEntry &entry, InputContextEvent &event) {
    reset(entry, event);
    state(event.inputContext())->localHistory.clear();
    state(event.inputContext())->clearPendingHistory();
}
void Engine::activate(const InputMethodEntry &entry, InputContextEvent &event) {
    auto *ic = event.inputContext();
    state(ic)->pinyin.setUseShuangpin(entry.uniqueName() == "phono-shuangpin");
    // These modules own their native status actions, shortcuts, punctuation
    // mappings, quote state, numeric exceptions and full-width commit filter.
    for (const auto *name : {"punctuation", "fullwidth"}) {
        instance_->addonManager().addon(name, true);
        if (auto *action = instance_->userInterfaceManager().lookupAction(name))
            ic->statusArea().addAction(StatusGroup::InputMethod, action);
    }
    // Fallback history cannot track application edits while unfocused.
    state(event.inputContext())->localHistory.clear();
    state(event.inputContext())->clearPendingHistory();
    update(event.inputContext(), true);
}
void Engine::invokeActionImpl(const InputMethodEntry &, InvokeActionEvent &event) {
    if (event.cursor() < 0 || event.action() != InvokeActionEvent::Action::LeftClick) return;
    auto *s = state(event.inputContext());
    const auto selected = utf8::length(s->pinyin.selectedSentence());
    if (static_cast<size_t>(event.cursor()) < selected) {
        s->pinyin.cancel();
    } else {
        const auto position = s->pinyin.selectedLength() + event.cursor() - selected;
        if (position > s->pinyin.size()) return;
        s->pinyin.setCursor(position);
    }
    event.filter();
    update(event.inputContext(), true);
}
void Engine::setConfig(const RawConfig &raw) {
    config_.load(raw, true);
    safeSaveAsIni(config_, "conf/phono.conf");
    configChanged();
}
void Engine::configChanged() {
    ime_->setShuangpinProfile(std::make_shared<libime::ShuangpinProfile>(
        static_cast<libime::ShuangpinBuiltinProfile>(*config_.shuangpin)));
    instance_->inputContextManager().foreach([this](InputContext *ic) {
        if (ic->property("phonoState")) {
            auto *s = state(ic);
            s->invalidate();
            s->clearPendingHistory();
            const auto *entry = instance_->inputMethodEntry(ic);
            if (phonoEntry(entry)) update(ic, true);
        }
        return true;
    });
}
void Engine::reloadConfig() {
    RawConfig raw;
    readAsIni(raw, "conf/phono.conf");
    config_.load(raw);
    configChanged();
}
void Engine::save() {
    safeSaveAsIni(config_, "conf/phono.conf");
    for (const auto &name : {"phono/user.dict", "phono/user.history"})
        saveUserData(name, [this, name](int fd) {
            try {
                OutputFDStreamBuf buffer(fd);
                std::ostream out(&buffer);
                if (std::string(name).ends_with("dict"))
                    ime_->dict()->save(libime::PinyinDictionary::UserDict, out, libime::PinyinDictFormat::Binary);
                else ime_->model()->save(out);
                out.flush();
                return static_cast<bool>(out);
            } catch (const std::exception &e) { FCITX_WARN() << "Phono: " << e.what(); return false; }
        });
}
} // namespace phono_fcitx
