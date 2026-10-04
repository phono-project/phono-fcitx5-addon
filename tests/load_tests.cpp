#include "check.h"
#include "addon.h"
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/candidatelist.h>
#include <fcitx/instance.h>
#include <cstdlib>
#include <iostream>
using namespace fcitx;
class Context : public InputContext {
public:
    explicit Context(InputContextManager &manager) : InputContext(manager, "phono-load-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
    void commitStringImpl(const std::string &text) override { committed += text; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override { ++preeditUpdates; }
    std::string committed;
    size_t preeditUpdates = 0;
};
int main() {
    try {
        char name[] = "phono-load-test", disable[] = "--disable=all", enable[] = "--enable=phono";
        char *argv[] = {name, disable, enable, nullptr};
        Instance instance(3, argv);
        instance.addonManager().registerDefaultLoader(nullptr);
        instance.initialize();
        auto *addon = instance.addonManager().addon("phono", true);
        CHECK(addon);
        auto *engine = dynamic_cast<InputMethodEngine *>(addon);
        CHECK(engine);
        const auto *entry = instance.inputMethodManager().entry("phono");
        CHECK(entry);
        CHECK(entry->addon() == "phono");
        auto group = instance.inputMethodManager().currentGroup();
        group.inputMethodList().clear();
        group.inputMethodList().push_back(InputMethodGroupItem("phono"));
        group.setDefaultInputMethod("phono");
        instance.inputMethodManager().setGroup(std::move(group));
        Context ic(instance.inputContextManager());
        ic.setCapabilityFlags({CapabilityFlag::Preedit, CapabilityFlag::SurroundingText,
                               CapabilityFlag::ClientUnfocusCommit});
        ic.focusIn();
        CHECK(instance.inputMethod(&ic) == "phono");
        RawConfig config;
        const auto *model = std::getenv("PHONO_TEST_MODEL_DIR");
        const bool real = model && *model;
        config.setValueByPath("Enabled", real ? "True" : "False");
        if (real) config.setValueByPath("ModelDirectory", model);
        engine->setConfig(config);
        ic.surroundingText().setText("今天", 2, 2);
        ic.updateSurroundingText();
        for (char c : std::string("nihao")) {
            KeyEvent event(&ic, Key(static_cast<KeySym>(c)), false);
            CHECK(ic.keyEvent(event));
        }
        // Observe model readiness internally; candidates must have no branding.
        auto *state = static_cast<phono_fcitx::State *>(ic.property("phonoState"));
        CHECK(state);
        const auto typedPreeditUpdates = ic.preeditUpdates;
        auto checkUnchangedSurrounding = [&] {
            const auto generation = state->generation;
            const auto token = state->cancelled;
            const auto list = ic.inputPanel().candidateList();
            const auto page = list->toPageable()->currentPage();
            const auto cursor = list->cursorIndex();
            for (int i = 0; i < 20; ++i) ic.updateSurroundingText();
            CHECK(state->generation == generation);
            CHECK(state->cancelled == token);
            CHECK(ic.inputPanel().candidateList() == list);
            CHECK(list->toPageable()->currentPage() == page);
            CHECK(list->cursorIndex() == cursor);
            CHECK(ic.preeditUpdates == typedPreeditUpdates);
        };
        // Repeated notifications while inference is pending must not cancel it.
        checkUnchangedSurrounding();
        std::string failure;
        int phase = 0;
        const auto deadline = now(CLOCK_MONOTONIC) + 10000000;
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0,
            [&](EventSourceTime *timer, uint64_t) {
                try {
                    CHECK(now(CLOCK_MONOTONIC) < deadline);
                    auto list = ic.inputPanel().candidateList();
                    CHECK(list && !list->empty());
                    if (real && state->beams.empty()) {
                        timer->setTime(now(CLOCK_MONOTONIC) + 2000); timer->setOneShot(); return true;
                    }
                    for (int i = 0; i < list->size(); ++i) CHECK(candidateCommentEmpty(list->candidate(i)));
                    CHECK(ic.preeditUpdates == typedPreeditUpdates);
                    if (!phase && real) {
                        CHECK(list->toPageable()->hasNext());
                        list->toPageable()->next();
                        list->toCursorMovable()->nextCandidate();
                        checkUnchangedSurrounding();
                        // Changes after the cursor do not alter model history.
                        ic.surroundingText().setText("今天尾巴", 2, 2);
                        checkUnchangedSurrounding();
                        // Exercise the real surrounding update event on a live
                        // loaded engine, while retaining the current composition.
                        const auto generation = state->generation;
                        const auto token = state->cancelled;
                        ic.surroundingText().setText("天气很好", 4, 4);
                        ic.updateSurroundingText();
                        CHECK(state->generation > generation);
                        CHECK(token->load());
                        CHECK(state->beams.empty());
                        CHECK(ic.preeditUpdates == typedPreeditUpdates);
                        checkUnchangedSurrounding();
                        phase = 1;
                        timer->setTime(now(CLOCK_MONOTONIC) + 2000); timer->setOneShot(); return true;
                    }
                    const auto expected = list->candidate(0).text().toString();
                    KeyEvent choose(&ic, Key(FcitxKey_space), false);
                    CHECK(ic.keyEvent(choose));
                    CHECK(ic.committed == expected);
                    CHECK(ic.inputPanel().clientPreedit().empty());
                    const auto firstCommit = *state->pendingHistory;
                    KeyEvent secondInput(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(secondInput));
                    KeyEvent secondCommit(&ic, Key(FcitxKey_Return), false); CHECK(ic.keyEvent(secondCommit));
                    CHECK(ic.committed == expected + "n");
                    KeyEvent followup(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(followup));
                    CHECK(state->pendingHistory);
                    const auto optimistic = *state->pendingHistory;
                    const auto generation = state->generation;
                    const auto token = state->cancelled;
                    if (real) CHECK(state->lastUpdate->history == optimistic);
                    // A repeated pre-commit surrounding snapshot is not an
                    // acknowledgement, and must not roll history backwards.
                    ic.updateSurroundingText();
                    CHECK(state->pendingHistory && *state->pendingHistory == optimistic);
                    CHECK(state->generation == generation && state->cancelled == token);
                    // The first of two commits may be confirmed separately,
                    // with an unrelated change on the right side of the cursor.
                    ic.surroundingText().setText(firstCommit + "尾巴", utf8::length(firstCommit), utf8::length(firstCommit));
                    ic.updateSurroundingText();
                    CHECK(state->pendingHistory && *state->pendingHistory == optimistic);
                    CHECK(state->generation == generation && state->cancelled == token);
                    ic.surroundingText().invalidate();
                    ic.updateSurroundingText();
                    CHECK(state->pendingHistory && *state->pendingHistory == optimistic);
                    CHECK(state->generation == generation && state->cancelled == token);
                    ic.surroundingText().setText(optimistic, utf8::length(optimistic), utf8::length(optimistic));
                    ic.updateSurroundingText();
                    CHECK(!state->pendingHistory);
                    CHECK(state->generation == generation && state->cancelled == token);
                    KeyEvent clear(&ic, Key(FcitxKey_Escape), false); CHECK(ic.keyEvent(clear));
                    // Losing surrounding support temporarily must preserve the
                    // last confirmed prefix, including after composition Escape.
                    ic.surroundingText().invalidate();
                    ic.updateSurroundingText();
                    CHECK(state->localHistory == optimistic);
                    KeyEvent cachedInput(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(cachedInput));
                    if (real) CHECK(state->lastUpdate->history == optimistic);
                    KeyEvent cachedClear(&ic, Key(FcitxKey_Escape), false); CHECK(ic.keyEvent(cachedClear));
                    CHECK(state->localHistory == optimistic);
                    // Moving back into the already updated document is an edit,
                    // even when its prefix happens to match an older commit.
                    ic.surroundingText().setText(optimistic, utf8::length(optimistic), utf8::length(optimistic));
                    ic.updateSurroundingText();
                    KeyEvent thirdInput(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(thirdInput));
                    KeyEvent thirdCommit(&ic, Key(FcitxKey_Return), false); CHECK(ic.keyEvent(thirdCommit));
                    CHECK(state->pendingHistory);
                    const auto document = *state->pendingHistory;
                    ic.surroundingText().setText(document, utf8::length(optimistic), utf8::length(optimistic));
                    ic.updateSurroundingText();
                    CHECK(!state->pendingHistory && state->localHistory == optimistic);
                    KeyEvent movedInput(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(movedInput));
                    if (real) CHECK(state->lastUpdate->history == optimistic);
                    KeyEvent movedClear(&ic, Key(FcitxKey_Escape), false); CHECK(ic.keyEvent(movedClear));
                    // A cursor move in a partially confirmed document must also
                    // win over the unconfirmed remainder of a later commit.
                    for (int i = 0; i < 2; ++i) {
                        KeyEvent input(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(input));
                        KeyEvent commit(&ic, Key(FcitxKey_Return), false); CHECK(ic.keyEvent(commit));
                    }
                    CHECK(state->pendingHistory);
                    const auto partialDocument = optimistic + "n尾巴";
                    ic.surroundingText().setText(partialDocument, utf8::length(optimistic) + 1, utf8::length(optimistic) + 1);
                    ic.updateSurroundingText();
                    CHECK(state->pendingHistory);
                    ic.surroundingText().setText(partialDocument, utf8::length(optimistic), utf8::length(optimistic));
                    ic.updateSurroundingText();
                    CHECK(!state->pendingHistory && state->localHistory == optimistic);
                    // Reconfiguration goes through the actual module boundary.
                    config.setValueByPath("Enabled", "False");
                    engine->setConfig(config);
                    KeyEvent typed(&ic, Key(FcitxKey_n), false); CHECK(ic.keyEvent(typed));
                    CHECK(state->beams.empty());
                    CHECK(candidateCommentEmpty(ic.inputPanel().candidateList()->candidate(0)));
                    ic.focusOut();
                    CHECK(ic.inputPanel().empty());
                    instance.eventLoop().exit(); return false;
                } catch (const std::exception &e) {
                    failure = e.what(); instance.eventLoop().exit(); return false;
                }
            });
        instance.eventLoop().exec();
        if (!failure.empty()) throw std::runtime_error(failure);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
