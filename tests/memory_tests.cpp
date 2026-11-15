#include "addon.h"
#include "check.h"
#include <fcitx-utils/event.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <getopt.h>

using namespace fcitx;
using namespace phono_fcitx;
class Context : public InputContext {
public:
    explicit Context(InputContextManager &manager) : InputContext(manager, "memory-test") { created(); }
    ~Context() override { destroy(); }
    const char *frontend() const override { return "test"; }
    void commitStringImpl(const std::string &text) override { committed += text; }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
    std::string committed;
};
void run(int phase, int scenario = 0) {
    optind = 0;
    char name[] = "memory-test", disable[] = "--disable=all";
    char *argv[] = {name, disable, nullptr};
    Instance instance(2, argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    instance.initialize();
    Engine engine(&instance, [phase](const Request &) {
        return phase == 5 ? Result{{"模型", "模型二"}, {}, 3} : Result{{"倪好嘛", "模型"}, {}, 3};
    });
    Context ic(instance.inputContextManager());
    ic.setCapabilityFlags({CapabilityFlag::Preedit});
    ic.focusIn();
    InputMethodEntry entry("phono", "Phono", "zh_CN", "phono");
    RawConfig config;
    config.setValueByPath("Enabled", "False");
    config.setValueByPath("PreferUnigramSingleSyllable", "False");
    config.setValueByPath("UserWordsEnabled", "True");
    config.setValueByPath("UserWordsFirst", "True");
    config.setValueByPath("UserWordsExactOnly", "True");
    config.setValueByPath("Learning", "True");
    engine.setConfig(config);
    auto key = [&](KeySym symbol) {
        KeyEvent event(&ic, Key(symbol), false);
        engine.keyEvent(entry, event);
        CHECK(event.accepted());
    };
    auto type = [&](const std::string &text) { for (unsigned char c : text) key(static_cast<KeySym>(c)); };
    auto first = [&] { return ic.inputPanel().candidateList()->candidate(0).text().toString(); };
    auto *state = engine.state(&ic);
    auto choose = [&](const std::string &text) {
        const auto &candidates = state->pinyin.candidatesToCursor();
        for (size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].toString() == text) { engine.select(&ic, state->generation, false, i); return; }
        throw std::runtime_error("missing phrase component: " + text);
    };
    auto waitModel = [&] {
        const auto deadline = now(CLOCK_MONOTONIC) + 2000000;
        bool ready = false;
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0,
            [&](EventSourceTime *timer, uint64_t) {
                ready = !state->beams.empty();
                if (ready || now(CLOCK_MONOTONIC) > deadline) { instance.eventLoop().exit(); return false; }
                timer->setTime(now(CLOCK_MONOTONIC) + 2000); timer->setOneShot(); return true;
            });
        instance.eventLoop().exec(); CHECK(ready);
    };
    if (phase == 5) {
        type("nihaoma"); choose("倪"); choose("好"); choose("嘛");
        const bool pairs = scenario & 4, enabled = scenario & 2, firstWord = scenario & 1;
        InputMethodEntry mode(pairs ? "phono-shuangpin" : "phono", "Phono", "zh_CN", "phono");
        InputContextEvent activated(&ic, EventType::InputContextFocusIn);
        engine.activate(mode, activated);
        const auto input = pairs ? "nihcma" : "nihaoma";
        type(input); CHECK(first() == "倪好嘛");
        // Disabling and re-enabling affects an existing composition, without erasing its memory.
        config.setValueByPath("UserWordsEnabled", "False"); engine.setConfig(config); engine.activate(mode, activated);
        CHECK(state->pinyin.userInput() == input && first() != "倪好嘛");
        config.setValueByPath("UserWordsEnabled", "True"); engine.setConfig(config); engine.activate(mode, activated);
        CHECK(first() == "倪好嘛"); key(FcitxKey_Escape);
        config.setValueByPath("Enabled", "True");
        config.setValueByPath("ModelDirectory", "/test/model");
        config.setValueByPath("Learning", "False");
        config.setValueByPath("UserWordsEnabled", enabled ? "True" : "False");
        config.setValueByPath("UserWordsFirst", firstWord ? "True" : "False");
        const bool englishFirst = scenario & 8;
        config.setValueByPath("DefaultDictionary/English/BeforeModel", englishFirst ? "True" : "False");
        config.setValueByPath("DefaultDictionary/English/MaximumDistance", "0");
        engine.setConfig(config); engine.activate(mode, activated);
        type(input); waitModel();
        std::vector<std::string> expected;
        if (enabled && firstWord) expected.push_back("倪好嘛");
        if (englishFirst) expected.push_back(input);
        expected.insert(expected.end(), {"模型", "模型二"});
        if (enabled && !firstWord) expected.push_back("倪好嘛");
        if (!englishFirst) expected.push_back(input);
        auto ordered = ic.inputPanel().candidateList();
        for (size_t i = 0; i < expected.size(); ++i)
            CHECK(ordered->candidate(i).text().toString() == expected[i]);
        if (enabled) {
            auto list = ic.inputPanel().candidateList();
            int index = 0;
            while (index < list->size() && list->candidate(index).text().toString() != "倪好嘛") ++index;
            CHECK(index < list->size());
            for (int i = 0; i < index; ++i) key(FcitxKey_Down);
            const auto cancelled = state->cancelled;
            key(FcitxKey_Delete);
            CHECK(state->pinyin.userInput() == input && state->beams.empty());
            CHECK(state->cancelled && state->cancelled != cancelled);
        }
    } else if (phase == 4) {
        type("nihao"); choose("倪"); choose("好");
        config.setValueByPath("Enabled", "True");
        config.setValueByPath("ModelDirectory", "/test/model");
        const bool pairs = scenario & 4, exactOnly = scenario & 2, longer = scenario & 1;
        config.setValueByPath("UserWordsExactOnly", exactOnly ? "True" : "False");
        engine.setConfig(config);
        InputMethodEntry mode(pairs ? "phono-shuangpin" : "phono", "Phono", "zh_CN", "phono");
        InputContextEvent activated(&ic, EventType::InputContextFocusIn);
        engine.activate(mode, activated);
        type(pairs ? (longer?"nihcma":"nihc") : (longer?"nihaoma":"nihao"));
        waitModel();
        CHECK(first() == ((!exactOnly || !longer) ? "倪好" : "倪好嘛"));
        key(FcitxKey_Escape);
    } else if (phase == 3) {
        config.setValueByPath("UserWordsExactOnly", "False"); engine.setConfig(config);
        type("ni"); choose("倪");
        type("ni"); CHECK(first() == "倪"); key(FcitxKey_Escape);
        type("nihaoma"); CHECK(first() != "倪"); key(FcitxKey_Escape);
        InputMethodEntry pairs("phono-shuangpin", "Phono", "zh_CN", "phono");
        InputContextEvent activated(&ic, EventType::InputContextFocusIn);
        engine.activate(pairs, activated);
        type("nihcma"); CHECK(first() != "倪");
        key(FcitxKey_Escape);
    } else if (phase == 0) {
        type("nihaoma");
        choose("倪"); choose("好"); choose("嘛");
        CHECK(ic.committed == "倪好嘛");
        type("nihaoma"); CHECK(first() == "倪好嘛");
        key(FcitxKey_space);
        // Cancellation does not learn a partially selected phrase.
        type("nihaoma"); choose("倪"); key(FcitxKey_Escape);
    } else if (phase == 1) {
        config.setValueByPath("Enabled", "True");
        config.setValueByPath("ModelDirectory", "/test/model");
        engine.setConfig(config);
        type("nihaoma"); CHECK(first() == "倪好嘛");
        const auto deadline = now(CLOCK_MONOTONIC) + 2000000;
        bool ready = false;
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0,
            [&](EventSourceTime *timer, uint64_t) {
                ready = !state->beams.empty();
                if (ready || now(CLOCK_MONOTONIC) > deadline) { instance.eventLoop().exit(); return false; }
                timer->setTime(now(CLOCK_MONOTONIC) + 2000); timer->setOneShot(); return true;
            });
        instance.eventLoop().exec(); CHECK(ready);
        CHECK(first() == "倪好嘛");
        // A duplicate model candidate must retain the dictionary's removable identity.
        key(FcitxKey_Delete);
        CHECK(state->pinyin.userInput() == "nihaoma");
        CHECK(ic.committed.empty());
        CHECK(first() != "倪好嘛");
    } else {
        type("nihaoma"); CHECK(first() != "倪好嘛");
        key(FcitxKey_Escape);
        // Learn again and use the same record through shuangpin.
        type("nihaoma"); choose("倪"); choose("好"); choose("嘛");
        InputMethodEntry pairs("phono-shuangpin", "Phono", "zh_CN", "phono");
        InputContextEvent activated(&ic, EventType::InputContextFocusIn);
        engine.activate(pairs, activated);
        type("nihcma"); CHECK(first() == "倪好嘛");
        key(FcitxKey_Delete); CHECK(state->pinyin.userInput() == "nihcma");
        CHECK(first() != "倪好嘛");
    }
}
int main() {
    try {
        const auto file = std::filesystem::path(std::getenv("XDG_DATA_HOME")) / "fcitx5/phono/learning.json";
        std::filesystem::remove(file);
        // A small isolated dictionary makes user/model/English collisions
        // deterministic for both pinyin and shuangpin in the existing matrix.
        const auto english = file.parent_path() / "english.tsv";
        std::filesystem::create_directories(file.parent_path());
        { std::ofstream out(english); out << "nihcma 35\nnihaoma 35\n"; }
        for (int scenario = 0; scenario < 16; ++scenario) {
            run(5, scenario);
            { std::ifstream in(file); const auto data = nlohmann::json::parse(in).at("selections");
              CHECK(data.size() == ((scenario & 2) ? 0 : 1)); }
            std::filesystem::remove(file);
        }
        std::filesystem::remove(english);
        for (int scenario = 0; scenario < 8; ++scenario) {
            run(4, scenario);
            std::filesystem::remove(file);
        }
        std::filesystem::remove(file);
        run(3);
        std::filesystem::remove(file);
        run(0);
        { std::ifstream in(file); const auto data = nlohmann::json::parse(in);
          CHECK(data.at("selections").at("ni'hao'ma\t倪好嘛") == 2);
          CHECK(data.at("selections").size() == 1); }
        run(1);
        { std::ifstream in(file); CHECK(nlohmann::json::parse(in).at("selections").empty()); }
        run(2);
        { std::ifstream in(file); CHECK(nlohmann::json::parse(in).at("selections").empty()); }
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
