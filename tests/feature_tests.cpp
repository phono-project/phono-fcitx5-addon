#include "addon.h"
#include "check.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>

using namespace fcitx;
using namespace phono_fcitx;

class Context : public InputContextV2 {
public:
    explicit Context(InputContextManager &manager) : InputContextV2(manager, "phono-feature-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
    void commitStringImpl(const std::string &text) override { committed += text; }
    void commitStringWithCursorImpl(const std::string &text, size_t cursor) override {
        committed += text;
        cursorCommits.push_back({text, cursor});
    }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const ForwardKeyEvent &event) override { forwarded.push_back(event.key().sym()); }
    void updatePreeditImpl() override {}
    std::string committed;
    std::vector<std::pair<std::string, size_t>> cursorCommits;
    std::vector<KeySym> forwarded;
};

int main() {
    try {
        char name[] = "phono-feature-test", disable[] = "--disable=all",
             enable[] = "--enable=phono,punctuation,fullwidth";
        char *argv[] = {name, disable, enable, nullptr};
        Instance instance(3, argv);
        instance.addonManager().registerDefaultLoader(nullptr);
        instance.initialize();
        auto *engine = dynamic_cast<InputMethodEngine *>(instance.addonManager().addon("phono", true));
        CHECK(engine);
        CHECK(instance.inputMethodManager().entry("phono"));
        CHECK(instance.inputMethodManager().entry("phono-shuangpin"));
        auto *punctuation = instance.addonManager().addon("punctuation", true);
        auto *fullwidth = instance.addonManager().addon("fullwidth", true);
        CHECK(punctuation && fullwidth);
        RawConfig punctuationConfig;
        punctuationConfig.setValueByPath("Enabled", "True");
        punctuationConfig.setValueByPath("HalfWidthPuncAfterLetterOrNumber", "True");
        punctuationConfig.setValueByPath("TypePairedPunctuationsTogether", "False");
        punctuation->setConfig(punctuationConfig);
        // The standard fullwidth module owns this configurable shortcut. Set a
        // conventional key explicitly in this test's isolated native config.
        RawConfig fullwidthConfig;
        fullwidthConfig.setValueByPath("Hotkey/0", "Shift+space");
        fullwidth->setConfig(fullwidthConfig);
        auto group = instance.inputMethodManager().currentGroup();
        group.inputMethodList().clear();
        group.inputMethodList().push_back(InputMethodGroupItem("phono-shuangpin"));
        group.inputMethodList().push_back(InputMethodGroupItem("phono"));
        group.setDefaultInputMethod("phono-shuangpin");
        instance.inputMethodManager().setGroup(std::move(group));
        Context ic(instance.inputContextManager());
        ic.setCapabilityFlags({CapabilityFlag::Preedit, CapabilityFlag::CommitStringWithCursor,
                               CapabilityFlag::ClientUnfocusCommit});
        ic.focusIn();
        CHECK(instance.inputMethod(&ic) == "phono-shuangpin");
        auto *state = static_cast<State *>(ic.property("phonoState"));
        CHECK(state && state->pinyin.useShuangpin());
        auto *punctuationAction = instance.userInterfaceManager().lookupAction("punctuation");
        auto *fullwidthAction = instance.userInterfaceManager().lookupAction("fullwidth");
        CHECK(punctuationAction && fullwidthAction);
        const auto actions = ic.statusArea().actions(StatusGroup::InputMethod);
        CHECK(std::find(actions.begin(), actions.end(), punctuationAction) != actions.end());
        CHECK(std::find(actions.begin(), actions.end(), fullwidthAction) != actions.end());
        CHECK(fullwidthAction->icon(&ic) == "fcitx-fullwidth-inactive");
        auto key = [&](KeySym symbol, KeyStates modifiers = KeyStates{}) {
            KeyEvent event(&ic, Key(symbol, modifiers), false);
            return ic.keyEvent(event);
        };
        auto type = [&](const std::string &raw) {
            for (unsigned char c : raw) CHECK(key(static_cast<KeySym>(c)));
        };
        auto first = [&]() -> const CandidateWord & {
            const auto list = ic.inputPanel().candidateList();
            CHECK(list && !list->empty());
            return list->candidate(0);
        };
        RawConfig config;
        const char *model = std::getenv("PHONO_TEST_MODEL_DIR");
        const bool real = model && *model;
        config.setValueByPath("Enabled", real ? "True" : "False");
        config.setValueByPath("ShuangpinScheme", "Natural Code");
        config.setValueByPath("InvalidInput", "Pass through");
        if (real) config.setValueByPath("ModelDirectory", model);
        engine->setConfig(config);
        std::deque<std::function<bool()>> steps;
        steps.push_back([&] {
            type("nihk");
            CHECK(state->pinyin.userInput() == "nihk");
            CHECK(state->pinyin.useShuangpin());
            return true;
        });
        steps.push_back([&] {
            if (real && state->beams.empty()) return false;
            const auto expected = first().text().toStringForCommit();
            CHECK(!expected.empty());
            const auto previous = ic.committed;
            // Native key dispatch commits the current candidate and then maps
            // the same punctuation key; no second key event is needed.
            CHECK(key(FcitxKey_comma));
            CHECK(ic.committed == previous + expected + "，");
            CHECK(state->pinyin.empty());
            config.setValueByPath("Enabled", "False");
            engine->setConfig(config);

            config.setValueByPath("ShuangpinScheme", "Microsoft");
            engine->setConfig(config);
            const auto semicolonBefore = ic.committed;
            CHECK(key(FcitxKey_semicolon));
            CHECK(ic.committed == semicolonBefore + "；");
            CHECK(state->pinyin.empty());
            type("n;");
            CHECK(state->pinyin.userInput() == "n;");
            CHECK(key(FcitxKey_Escape));
            config.setValueByPath("ShuangpinScheme", "Xiaohe");
            engine->setConfig(config);

            const auto before = ic.committed;
            CHECK(key(FcitxKey_comma));
            CHECK(key(FcitxKey_period));
            CHECK(key(FcitxKey_question, KeyState::Shift));
            CHECK(key(FcitxKey_exclam, KeyState::Shift));
            CHECK(ic.committed == before + "，。？！");
            CHECK(key(FcitxKey_quotedbl, KeyState::Shift));
            CHECK(key(FcitxKey_quotedbl, KeyState::Shift));
            CHECK(ic.committed.ends_with("“”"));

            // Native punctuation remembers forwarded digits and leaves the
            // next decimal point untouched for the application to receive.
            const auto decimalBefore = ic.committed;
            CHECK(!key(FcitxKey_3));
            CHECK(!key(FcitxKey_period));
            CHECK(ic.committed == decimalBefore);
            CHECK(!key(FcitxKey_KP_Decimal));
            CHECK(!key(FcitxKey_KP_Divide));
            CHECK(ic.committed == decimalBefore);
            CHECK(key(FcitxKey_comma)); // preceding keypad slash resets digits

            CHECK(key(FcitxKey_period, KeyState::Ctrl));
            CHECK(punctuationAction->icon(&ic) == "fcitx-punc-inactive");
            const auto disabledBefore = ic.committed;
            CHECK(!key(FcitxKey_comma));
            CHECK(ic.committed == disabledBefore);
            CHECK(key(FcitxKey_period, KeyState::Ctrl));
            CHECK(punctuationAction->icon(&ic) == "fcitx-punc-active");
            CHECK(key(FcitxKey_comma));
            CHECK(ic.committed == disabledBefore + "，");

            punctuationConfig.setValueByPath("TypePairedPunctuationsTogether", "True");
            punctuation->setConfig(punctuationConfig);
            const auto cursorBefore = ic.cursorCommits.size();
            CHECK(key(FcitxKey_quotedbl, KeyState::Shift));
            CHECK(ic.cursorCommits.size() == cursorBefore + 1);
            CHECK(ic.cursorCommits.back().first == "“”");
            CHECK(ic.cursorCommits.back().second == 1);
            auto capabilities = ic.capabilityFlags().unset(CapabilityFlag::CommitStringWithCursor);
            ic.setCapabilityFlags(capabilities);
            const auto forwardedBefore = ic.forwarded.size();
            CHECK(key(FcitxKey_quotedbl, KeyState::Shift));
            CHECK(ic.committed.ends_with("“”"));
            CHECK(ic.forwarded.size() == forwardedBefore + 1);
            CHECK(ic.forwarded.back() == FcitxKey_Left);
            ic.setCapabilityFlags(capabilities | CapabilityFlag::CommitStringWithCursor);

            CHECK(key(FcitxKey_space, KeyState::Shift));
            CHECK(fullwidthAction->icon(&ic) == "fcitx-fullwidth-active");
            const auto fullBefore = ic.committed;
            CHECK(key(FcitxKey_a));
            // Fullwidth keeps Chinese composition active. A raw Enter commit
            // passes through the native ASCII-to-fullwidth commit filter.
            CHECK(state->pinyin.userInput() == "a");
            CHECK(key(FcitxKey_Return));
            CHECK(key(FcitxKey_9));
            CHECK(key(FcitxKey_space));
            if (ic.committed != fullBefore + "ａ９　")
                throw std::runtime_error("Unexpected fullwidth output: " + ic.committed.substr(fullBefore.size()));
            CHECK(state->pinyin.empty());
            CHECK(key(FcitxKey_space, KeyState::Shift));
            CHECK(fullwidthAction->icon(&ic) == "fcitx-fullwidth-inactive");

            if (real) {
                config.setValueByPath("Enabled", "True");
                config.setValueByPath("InvalidInput", "Delete (show struck-out text)");
                engine->setConfig(config);
                type("ibni");
            }
            return true;
        });
        if (real) {
            steps.push_back([&] {
                if (state->beams.empty()) return false;
                CHECK(state->invalid.size() == 1);
                CHECK(state->invalid[0].text == "i");
                const auto &text = first().text();
                CHECK(text.toString().front() == 'i');
                CHECK(text.formatAt(0).test(TextFormatFlag::Strike));
                CHECK(text.formatAt(0).test(TextFormatFlag::DontCommit));
                const auto before = ic.committed;
                const auto expected = text.toStringForCommit();
                CHECK(key(FcitxKey_space));
                CHECK(ic.committed == before + expected);
                CHECK(!expected.empty() && expected.front() != 'i');
                config.setValueByPath("InvalidInput", "Pass through");
                engine->setConfig(config);
                type("ibni");
                return true;
            });
            steps.push_back([&] {
                if (state->beams.empty()) return false;
                CHECK(state->invalid.size() == 1);
                const auto &text = first().text();
                CHECK(text.toString().front() == 'i');
                CHECK(text.toStringForCommit() == text.toString());
                CHECK(!text.formatAt(0).test(TextFormatFlag::Strike));
                const auto before = ic.committed;
                const auto expected = text.toStringForCommit();
                CHECK(key(FcitxKey_space));
                CHECK(ic.committed == before + expected);
                return true;
            });
        }
        steps.push_back([&] {
            config.setValueByPath("Enabled", "False");
            engine->setConfig(config);
            instance.setCurrentInputMethod(&ic, "phono", true);
            CHECK(instance.inputMethod(&ic) == "phono");
            CHECK(!state->pinyin.useShuangpin());
            type("nihao");
            CHECK(state->pinyin.userInput() == "nihao");
            CHECK(first().text().toStringForCommit() == "你好");
            const auto before = ic.committed;
            CHECK(key(FcitxKey_period));
            CHECK(ic.committed == before + "你好。");
            CHECK(state->pinyin.empty());
            if (real) {
                config.setValueByPath("Enabled", "True");
                config.setValueByPath("InvalidInput", "Delete (show struck-out text)");
                engine->setConfig(config);
                type("ni'v'hao");
            }
            return true;
        });
        if (real) {
            steps.push_back([&] {
                if (state->beams.empty()) return false;
                CHECK(state->invalid.size() == 1);
                CHECK(state->invalid[0].text == "v");
                CHECK(state->invalid[0].beforeSyllable == 1);
                const auto &text = first().text();
                bool struck = false;
                for (size_t i = 0; i < text.size(); ++i) {
                    if (text.stringAt(i) != "v") continue;
                    CHECK(text.formatAt(i).test(TextFormatFlag::Strike));
                    CHECK(text.formatAt(i).test(TextFormatFlag::DontCommit));
                    struck = true;
                }
                CHECK(struck);
                const auto before = ic.committed;
                const auto expected = text.toStringForCommit();
                CHECK(key(FcitxKey_space));
                CHECK(ic.committed == before + expected);
                type("u'i'v");
                return true;
            });
            steps.push_back([&] {
                if (state->beams.empty()) return false;
                CHECK(first().text().toString() == "uiv");
                CHECK(first().text().toStringForCommit().empty());
                const auto before = ic.committed;
                CHECK(key(FcitxKey_space));
                CHECK(ic.committed == before);
                CHECK(state->pinyin.empty());
                instance.setCurrentInputMethod(&ic, "phono-shuangpin", true);
                CHECK(state->pinyin.useShuangpin());
                return true;
            });
        }
        steps.push_back([&] {
            instance.setCurrentInputMethod(&ic, "phono", true);
            config.setValueByPath("Enabled", real ? "True" : "False");
            engine->setConfig(config);
            type("ma");
            return true;
        });
        steps.push_back([&] {
            if (real && state->beams.empty()) return false;
            auto list = ic.inputPanel().candidateList();
            std::vector<std::string> texts;
            do {
                for (int i = 0; i < list->size(); ++i) texts.push_back(list->candidate(i).text().toStringForCommit());
                if (!list->toPageable()->hasNext()) break;
                CHECK(key(FcitxKey_Tab));
            } while (true);
            CHECK(std::count(texts.begin(), texts.end(), "吗") == 1);
            CHECK(std::count(texts.begin(), texts.end(), "嘛") == 1);
            CHECK(key(FcitxKey_Escape));
            return true;
        });
        std::string failure;
        const auto deadline = now(CLOCK_MONOTONIC) + 25000000;
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0,
            [&](EventSourceTime *timer, uint64_t) {
                try {
                    CHECK(now(CLOCK_MONOTONIC) < deadline);
                    if (!steps.empty() && steps.front()()) steps.pop_front();
                } catch (const std::exception &e) {
                    failure = e.what(); instance.eventLoop().exit(); return false;
                }
                if (steps.empty()) { instance.eventLoop().exit(); return false; }
                timer->setTime(now(CLOCK_MONOTONIC) + 2000); timer->setOneShot(); return true;
            });
        instance.eventLoop().exec();
        if (!failure.empty()) throw std::runtime_error(failure);
        ic.focusOut();
        CHECK(ic.inputPanel().empty());
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
