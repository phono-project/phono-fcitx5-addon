#include "addon.h"
#include "check.h"
#include <fcitx-utils/event.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/candidatelist.h>
#include <fcitx/addonmanager.h>
#include <deque>
#include <algorithm>
#include <iostream>
using namespace fcitx;
using namespace phono_fcitx;
class Context : public InputContext {
public:
    explicit Context(InputContextManager &manager) : InputContext(manager, "phono-test") { created(); }
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
        char name[] = "phono-test", disable[] = "--disable=all", enable[] = "--enable=keyboard";
        char *argv[] = {name, disable, enable, nullptr};
        Instance instance(3, argv);
        instance.addonManager().registerDefaultLoader(nullptr);
        instance.initialize();
        std::mutex mutex;
        std::vector<Request> requests;
        Engine engine(&instance, [&](const Request &r) {
            { std::lock_guard lock(mutex); requests.push_back(r); }
            if (r.pinyin == "ma") return Result{{"吗", "吗", "马"}, {}, 3};
            return Result{{"模型甲", "模型乙", "模型丙"}, {}, 3};
        });
        Context ic(instance.inputContextManager());
        ic.setCapabilityFlags({CapabilityFlag::Preedit, CapabilityFlag::SurroundingText});
        ic.focusIn();
        InputMethodEntry entry("phono", "Phono", "zh_CN", "phono");
        RawConfig config;
        config.setValueByPath("Enabled", "True");
        config.setValueByPath("ModelDirectory", "/test/model");
        engine.setConfig(config);
        auto key = [&](KeySym sym, KeyStates states = KeyStates{}) {
            KeyEvent event(&ic, Key(sym, states), false);
            engine.keyEvent(entry, event);
            return event.accepted();
        };
        auto type = [&](const std::string &text) { for (unsigned char c : text) CHECK(key(static_cast<KeySym>(c))); };
        auto reset = [&] { ResetEvent event(&ic); engine.reset(entry, event); };
        auto hasBeams = [&] { return engine.state(&ic)->beams.size() == 3; };
        std::deque<std::function<bool()>> steps;
        std::shared_ptr<CandidateList> stale;
        std::vector<std::string> classic;
        size_t splitIndex = 0;
        size_t before = 0;
        size_t typedPreeditUpdates = 0;
        steps.push_back([&] {
            // Surrounding indices are characters, including emoji and CJK.
            ic.surroundingText().setText("甲😀乙选中尾", 5, 3);
            type("nihaoma");
            typedPreeditUpdates = ic.preeditUpdates;
            for (const auto &c : engine.state(&ic)->pinyin.candidatesToCursor()) classic.push_back(c.toString());
            CHECK(classic.size() > 3);
            stale = ic.inputPanel().candidateList();
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            auto list = ic.inputPanel().candidateList();
            CHECK(list->candidate(0).text().toString() == "模型甲");
            for (int i = 0; i < 3; ++i) CHECK(candidateCommentEmpty(list->candidate(i)));
            CHECK(ic.preeditUpdates == typedPreeditUpdates);
            CHECK(list->candidate(3).text().toString() == classic[0]);
            const auto generation = engine.state(&ic)->generation;
            const auto token = engine.state(&ic)->cancelled;
            // Delete at the end does not change the composition or candidates.
            CHECK(key(FcitxKey_Delete));
            CHECK(engine.state(&ic)->generation == generation);
            CHECK(engine.state(&ic)->cancelled == token && !token->load());
            CHECK(ic.inputPanel().candidateList() == list);
            CHECK(ic.preeditUpdates == typedPreeditUpdates);
            { std::lock_guard lock(mutex); CHECK(requests.back().history == "甲😀乙"); }
            CHECK(key(FcitxKey_2));
            CHECK(ic.committed == "模型乙");
            CHECK(engine.state(&ic)->pinyin.empty());
            // An old UI selection is a no-op after the composition changes.
            stale->candidate(0).select(&ic);
            CHECK(ic.committed == "模型乙");
            type("nihaoma");
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            { std::lock_guard lock(mutex); CHECK(requests.back().history == "甲😀乙模型乙"); }
            const auto &candidates = engine.state(&ic)->pinyin.candidatesToCursor();
            for (size_t i = 3; i < candidates.size(); ++i)
                if (candidates[i].toString().size() == 3) { splitIndex = i; break; }
            CHECK(splitIndex >= 3);
            auto list = ic.inputPanel().candidateList();
            const auto uiIndex = splitIndex + 3;
            list->toPageable()->setPage(uiIndex / 7);
            CHECK(list->candidate(uiIndex % 7).text().toString() == candidates[splitIndex].toString());
            list->candidate(uiIndex % 7).select(&ic);
            CHECK(!engine.state(&ic)->pinyin.empty());
            CHECK(engine.state(&ic)->splitting);
            CHECK(engine.state(&ic)->beams.empty());
            { std::lock_guard lock(mutex); before = requests.size(); }
            CHECK(key(FcitxKey_BackSpace));
            CHECK(engine.state(&ic)->splitting);
            CHECK(engine.state(&ic)->beams.empty());
            CHECK(key(FcitxKey_space));
            reset();
            { std::lock_guard lock(mutex); CHECK(requests.size() == before); }
            // Editing at a middle cursor suspends full-sentence model proposals.
            type("nihao");
            CHECK(key(FcitxKey_Left));
            CHECK(engine.state(&ic)->beams.empty());
            CHECK(key(FcitxKey_End));
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            // Clearing config invalidates the active generation immediately.
            auto token = engine.state(&ic)->cancelled;
            RawConfig changed; changed.setValueByPath("Enabled", "False");
            engine.setConfig(changed);
            CHECK(token->load());
            CHECK(engine.state(&ic)->beams.empty());
            reset();
            changed.setValueByPath("Enabled", "True");
            engine.setConfig(changed);
            ic.setCapabilityFlags({CapabilityFlag::Password, CapabilityFlag::Preedit});
            { std::lock_guard lock(mutex); before = requests.size(); }
            type("nihao");
            CHECK(!hasBeams());
            CHECK(key(FcitxKey_Return));
            CHECK(engine.state(&ic)->localHistory.empty());
            { std::lock_guard lock(mutex); CHECK(requests.size() == before); }
            ic.setCapabilityFlags({CapabilityFlag::Preedit});
            type("shijie");
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            CHECK(key(FcitxKey_space));
            type("nihao");
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            { std::lock_guard lock(mutex); CHECK(requests.back().history == "模型甲"); }
            reset();
            type("ma");
            return true;
        });
        steps.push_back([&] {
            if (!hasBeams()) return false;
            auto list = ic.inputPanel().candidateList();
            CHECK(list->candidate(0).text().toString() == "吗");
            std::vector<std::string> texts;
            do {
                for (int i = 0; i < list->size(); ++i) texts.push_back(list->candidate(i).text().toString());
                if (!list->toPageable()->hasNext()) break;
                list->toPageable()->next();
            } while (true);
            CHECK(std::count(texts.begin(), texts.end(), "吗") == 1);
            CHECK(std::count(texts.begin(), texts.end(), "嘛") == 1);
            CHECK(std::count(texts.begin(), texts.end(), "马") == 1);
            list->toPageable()->setPage(0);
            auto page = [&] { return list->toPageable()->currentPage(); };
            CHECK(key(FcitxKey_plus) && page() == 1);
            CHECK(key(FcitxKey_minus) && page() == 0);
            CHECK(key(FcitxKey_Tab) && page() == 1);
            CHECK(key(FcitxKey_Tab, KeyState::Shift) && page() == 0);
            CHECK(key(FcitxKey_equal) && page() == 1);
            CHECK(key(FcitxKey_ISO_Left_Tab) && page() == 0);
            CHECK(key(FcitxKey_Page_Down) && page() == 1);
            CHECK(key(FcitxKey_ISO_Left_Tab, KeyState::Shift) && page() == 0);
            CHECK(key(FcitxKey_plus, KeyState::Shift) && page() == 1);
            CHECK(key(FcitxKey_underscore) && page() == 0);
            CHECK(key(FcitxKey_equal) && page() == 1);
            CHECK(key(FcitxKey_underscore, KeyState::Shift) && page() == 0);
            CHECK(key(FcitxKey_Page_Down) && page() == 1);
            CHECK(key(FcitxKey_Page_Up) && page() == 0);
            auto *common = dynamic_cast<CommonCandidateList *>(list.get());
            CHECK(common);
            common->setGlobalCursorIndex(0);
            CHECK(key(FcitxKey_Down) && page() == 0);
            CHECK(list->cursorIndex() == 1);
            CHECK(key(FcitxKey_Down) && page() == 0);
            CHECK(list->cursorIndex() == 2);
            CHECK(key(FcitxKey_Up) && page() == 0);
            CHECK(list->cursorIndex() == 1);
            CHECK(!key(FcitxKey_Tab, {KeyState::Ctrl, KeyState::Shift}) && page() == 0);
            // A preserved dictionary alternative must still select its own
            // original libime index after model deduplication changed positions.
            const auto index = std::find(texts.begin(), texts.end(), "嘛") - texts.begin();
            list->toPageable()->setPage(index / 7);
            const auto committed = ic.committed;
            list->candidate(index % 7).select(&ic);
            CHECK(ic.committed == committed + "嘛");
            CHECK(engine.state(&ic)->pinyin.empty());
            // Context destruction cancels pending results, and a second context
            // has a separate composition and fallback history.
            auto other = std::make_unique<Context>(instance.inputContextManager());
            other->focusIn();
            KeyEvent event(other.get(), Key(FcitxKey_n), false);
            engine.keyEvent(entry, event);
            auto token = engine.state(other.get())->cancelled;
            CHECK(token);
            other.reset();
            CHECK(token->load());
            reset();
            return true;
        });
        std::string failure;
        const auto deadline = now(CLOCK_MONOTONIC) + 5000000;
        auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC), 0,
            [&](EventSourceTime *timer, uint64_t) {
                try {
                    CHECK(now(CLOCK_MONOTONIC) < deadline);
                    if (!steps.empty() && steps.front()()) steps.pop_front();
                } catch (const std::exception &e) { failure = e.what(); instance.eventLoop().exit(); return false; }
                if (steps.empty()) { instance.eventLoop().exit(); return false; }
                timer->setTime(now(CLOCK_MONOTONIC) + 2000);
                timer->setOneShot();
                return true;
            });
        instance.eventLoop().exec();
        if (!failure.empty()) throw std::runtime_error(failure);
        // Native decimal parameters reject invalid text before it reaches JSON.
        Config native;
        RawConfig invalid; invalid.setValueByPath("TrialRatio", "nan"); native.load(invalid, true);
        CHECK(*native.trialRatio == "0.25");
        CHECK(!nlohmann::json::parse(settingsFor(native).contextJson).contains("beam_size"));
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
