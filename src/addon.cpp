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
#include <libime/pinyin/pinyinencoder.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
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
    bool forget(InputContext *ic) const { return !model_ && engine_->forget(ic, generation_, index_); }
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
    ime_->setShuangpinProfile(std::make_shared<libime::ShuangpinProfile>(
        static_cast<libime::ShuangpinBuiltinProfile>(*config_.shuangpin)));
    loadLearning();
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
void Engine::rankUnigram(State *s) {
    std::vector<std::string> readings;
    if (s->pinyin.useShuangpin()) {
        const auto converted = convertShuangpin(s->pinyin.userInput(), *ime_->shuangpinProfile());
        if (!converted.invalidSpans.empty() || converted.tokens.size() != 1 ||
            converted.tokens.front().end - converted.tokens.front().begin != 2) return;
        readings = converted.tokens.front().alternatives;
    } else {
        for (const auto &[initial, finals] : libime::PinyinEncoder::stringToSyllables(
                 s->pinyin.userInput(), libime::PinyinFuzzyFlag::None))
            for (const auto &[final, fuzzy] : finals)
                if (initial != libime::PinyinInitial::Invalid && final != libime::PinyinFinal::Invalid)
                    readings.push_back(libime::PinyinSyllable(initial, final).toString());
    }
    if (readings.empty()) return;
    const auto &classic = s->pinyin.candidatesToCursor();
    const auto *model = ime_->model();
    std::vector<std::pair<float, size_t>> ranked;
    for (size_t i = 0; i < classic.size(); ++i) {
        const auto &candidate = classic[i];
        if (candidate.size() != 1 || candidate.sentence().front()->to()->index() != s->pinyin.size() ||
            utf8::length(candidate.toString()) != 1) continue;
        const auto spelling = s->pinyin.candidateFullPinyin(candidate);
        if (std::find(readings.begin(), readings.end(), spelling) == readings.end()) continue;
        libime::State next;
        // The static model's null state yields P(word), without BOS or learned bigrams.
        const auto score = model->libime::LanguageModel::score(
            model->libime::LanguageModel::nullState(), *candidate.sentence().front(), next);
        ranked.emplace_back(score, i);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    for (const auto &[score, index] : ranked) s->unigramIndices.push_back(index);
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
    const bool clientPreedit = ic->capabilityFlags().test(CapabilityFlag::Preedit);
    const bool showClientPreedit = clientPreedit && *config_.showPreedit;
    panel.reset();
    if (!s->pinyin.empty()) {
        // Draw the same preedit in exactly one place: the client field when
        // inline preedit is enabled and supported, otherwise the input window.
        if (showClientPreedit) panel.setClientPreedit(preedit);
        else panel.setPreedit(preedit);
        auto list = std::make_unique<CommonCandidateList>();
        list->setPageSize(*config_.pageSize);
        list->setSelectionKey(selectionKeys());
        std::unordered_set<std::string> seen;
        const auto &classic = s->pinyin.candidatesToCursor();
        const auto remaining = s->pinyin.userInput().substr(s->pinyin.selectedLength());
        const auto converted = s->pinyin.useShuangpin()
            ? convertShuangpin(remaining, *ime_->shuangpinProfile()) : ShuangpinConversion{};
        auto exact = [&](size_t index) {
            const auto &sentence = classic[index].sentence();
            if (sentence.empty() || sentence.back()->to()->index() != s->pinyin.size()) return false;
            auto spelling = s->pinyin.candidateFullPinyin(classic[index]);
            if (!s->pinyin.useShuangpin()) {
                auto input = remaining;
                std::erase(input, '\''); std::erase(spelling, '\'');
                return input == spelling;
            }
            if (!converted.invalidSpans.empty()) return false;
            std::istringstream readings(spelling);
            std::string reading;
            size_t token = 0;
            while (std::getline(readings, reading, '\'')) {
                if (token >= converted.tokens.size()) return false;
                const auto &alternatives = converted.tokens[token++].alternatives;
                if (std::find(alternatives.begin(), alternatives.end(), reading) == alternatives.end()) return false;
            }
            return token == converted.tokens.size();
        };
        std::vector<std::pair<uint32_t, size_t>> personal;
        for (size_t i = 0; i < classic.size(); ++i) {
            const auto count = frequency(s->pinyin.candidateFullPinyin(classic[i]), classic[i].toString());
            const bool complete = !classic[i].sentence().empty() &&
                classic[i].sentence().back()->to()->index() == s->pinyin.size();
            if (*config_.userWordsEnabled && count && (complete || utf8::length(classic[i].toString()) != 1)) personal.emplace_back(count, i);
        }
        std::stable_sort(personal.begin(), personal.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[count, index] : personal)
            if (*config_.userWordsFirst && (!*config_.userWordsExactOnly || exact(index)) && seen.insert(classic[index].toString()).second)
                list->append<Word>(this, s->generation, false, index, Text(classic[index].toString()));
        for (size_t i = 0; i < s->beams.size(); ++i) {
            const auto presentation = presentCandidate(s->beams[i], s->invalid, *config_.invalidInput);
            if (seen.insert(presentation.committed).second)
                list->append<Word>(this, s->generation, true, i, candidateText(presentation));
        }
        for (const auto &[count, index] : personal)
            if (seen.insert(classic[index].toString()).second)
                list->append<Word>(this, s->generation, false, index, Text(classic[index].toString()));
        for (const auto i : s->unigramIndices)
            if (seen.insert(classic[i].toString()).second)
                list->append<Word>(this, s->generation, false, i, Text(classic[i].toString()));
        // Remaining native dictionary candidates follow model and user words.
        for (size_t i = 0; i < classic.size(); ++i)
            if (seen.insert(classic[i].toString()).second)
                list->append<Word>(this, s->generation, false, i, Text(classic[i].toString()));
        if (!list->empty()) list->setGlobalCursorIndex(0);
        panel.setCandidateList(std::move(list));
        if (!s->error.empty()) panel.setAuxDown(Text(s->error));
        else if (s->splitting) panel.setAuxDown(Text(_("Dictionary segmentation")));
    }
    ic->updatePreedit();
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
    if (eligible && *config_.preferUnigram && s->lastUpdate->history.empty()) rankUnigram(s);
    if (eligible && s->unigramIndices.empty() && config_.model->empty())
        s->error = _("Set the model package directory in Phono settings");
    render(ic);
    if (!eligible || !s->unigramIndices.empty() || config_.model->empty()) return;
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
    if (learn && *config_.learning && !sensitive(ic)) {
        const auto pinyin = s->pinyin.selectedFullPinyin();
        if (!pinyin.empty()) {
            auto &count = learned_[pinyin + "\t" + text];
            count = std::min(count + 1, uint32_t(100000));
            ime_->dict()->addWord(libime::PinyinDictionary::UserDict, pinyin, text);
            saveLearning();
        }
    }
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
    } else if (sym == FcitxKey_Delete) {
        const auto index = list && list->cursorIndex() >= 0 ? list->cursorIndex() : 0;
        if (list && index < list->size()) {
            const auto *word = dynamic_cast<const Word *>(&list->candidate(index));
            if (word && word->forget(ic)) { event.filterAndAccept(); return; }
        }
        s->pinyin.del();
    }
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
    const auto previousScheme = *config_.shuangpin;
    config_.load(raw, true);
    safeSaveAsIni(config_, "conf/phono.conf");
    configChanged(previousScheme != *config_.shuangpin);
}
void Engine::configChanged(bool shuangpinChanged) {
    ime_->dict()->setFlags(libime::PinyinDictionary::UserDict,
        *config_.userWordsEnabled ? libime::PinyinDictFlag::NoFlag : libime::PinyinDictFlag::Disabled);
    if (shuangpinChanged) ime_->setShuangpinProfile(std::make_shared<libime::ShuangpinProfile>(
        static_cast<libime::ShuangpinBuiltinProfile>(*config_.shuangpin)));
    instance_->inputContextManager().foreach([this](InputContext *ic) {
        if (ic->property("phonoState")) {
            auto *s = state(ic);
            s->invalidate();
            s->clearPendingHistory();
            if (!s->pinyin.empty()) {
                const auto cursor = s->pinyin.cursor();
                const auto begin = s->pinyin.selectedLength();
                const auto input = s->pinyin.userInput().substr(begin);
                s->pinyin.erase(begin, s->pinyin.size());
                s->pinyin.type(input);
                s->pinyin.setCursor(cursor);
            }
            const auto *entry = instance_->inputMethodEntry(ic);
            if (phonoEntry(entry)) update(ic, true);
        }
        return true;
    });
}
void Engine::reloadConfig() {
    const auto previousScheme = *config_.shuangpin;
    RawConfig raw;
    readAsIni(raw, "conf/phono.conf");
    config_.load(raw);
    configChanged(previousScheme != *config_.shuangpin);
}
void Engine::save() {
    safeSaveAsIni(config_, "conf/phono.conf");
    saveLearning();
}
uint32_t Engine::frequency(const std::string &pinyin, const std::string &text) const {
    const auto it = learned_.find(pinyin + "\t" + text);
    return it == learned_.end() ? 0 : it->second;
}
void Engine::loadLearning() {
    auto file = openUserData("phono/learning.json");
    try {
        if (file.isValid()) {
            InputFDStreamBuf buffer(file.fd());
            std::istream in(&buffer);
            learned_ = nlohmann::json::parse(in).at("selections").get<decltype(learned_)>();
        } else {
            // Import legacy vocabulary once. Dictionary weights are not selection counts.
            auto legacy = openUserData("phono/user.dict");
            if (legacy.isValid()) {
                InputFDStreamBuf buffer(legacy.fd());
                std::istream in(&buffer);
                libime::PinyinDictionary dictionary;
                dictionary.load(libime::PinyinDictionary::UserDict, in, libime::PinyinDictFormat::Binary);
                std::stringstream exported;
                dictionary.save(libime::PinyinDictionary::UserDict, exported, libime::PinyinDictFormat::Text);
                std::string line, text, pinyin;
                while (std::getline(exported, line)) {
                    std::istringstream fields(line);
                    if (fields >> std::quoted(text) >> pinyin) learned_[pinyin + "\t" + text] = 1;
                }
                saveLearning();
            }
        }
        for (const auto &[key, count] : learned_) {
            const auto separator = key.find('\t');
            if (count && separator != std::string::npos)
                ime_->dict()->addWord(libime::PinyinDictionary::UserDict, key.substr(0, separator), key.substr(separator + 1));
        }
    } catch (const std::exception &e) { FCITX_WARN() << "Phono: " << e.what(); }
}
void Engine::saveLearning() {
    saveUserData("phono/learning.json", [this](int fd) {
        OutputFDStreamBuf buffer(fd);
        std::ostream out(&buffer);
        out << nlohmann::json{{"schema_version", "1.0"}, {"selections", learned_}}.dump(2);
        out.flush();
        return static_cast<bool>(out);
    });
}
bool Engine::forget(InputContext *ic, uint64_t generation, size_t index) {
    auto *s = state(ic);
    const auto &candidates = s->pinyin.candidatesToCursor();
    if (!*config_.userWordsEnabled || generation != s->generation || index >= candidates.size()) return false;
    const auto text = candidates[index].toString();
    const auto pinyin = s->pinyin.candidateFullPinyin(candidates[index]);
    if (!learned_.erase(pinyin + "\t" + text)) return false;
    s->invalidate();
    ime_->dict()->removeWord(libime::PinyinDictionary::UserDict, pinyin, text);
    saveLearning();
    // Re-decode unchanged input using PinyinContext's existing editing API.
    const auto cursor = s->pinyin.cursor();
    const auto begin = s->pinyin.selectedLength();
    const auto input = s->pinyin.userInput().substr(begin);
    s->pinyin.erase(begin, s->pinyin.size());
    s->pinyin.type(input);
    s->pinyin.setCursor(cursor);
    update(ic, true);
    return true;
}
} // namespace phono_fcitx
