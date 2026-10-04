#include "worker.h"
#include "check.h"
#include <chrono>
#include <iostream>
#include <future>
using namespace phono_fcitx;
using namespace std::chrono_literals;
int main() {
    try {
        std::promise<void> started, release, done;
        auto gate = release.get_future().share();
        std::mutex mutex;
        std::vector<std::string> ran, completed;
        Worker worker([&](const Request &r) {
            if (r.pinyin == "old") { started.set_value(); gate.wait(); }
            { std::lock_guard lock(mutex); ran.push_back(r.pinyin); }
            return Result{{r.pinyin}, {}, 1};
        });
        auto complete = [&](Result result) {
            { std::lock_guard lock(mutex); completed.push_back(result.candidates.at(0)); }
            if (result.candidates.at(0) == "latest") done.set_value();
        };
        Request old; old.contextId = 1; old.pinyin = "old";
        auto oldToken = old.cancelled;
        worker.submit(old, complete);
        CHECK(started.get_future().wait_for(2s) == std::future_status::ready);
        Request pending; pending.contextId = 1; pending.pinyin = "superseded";
        auto pendingToken = pending.cancelled;
        worker.submit(pending, complete);
        Request other; other.contextId = 2; other.pinyin = "other";
        worker.submit(other, complete);
        Request latest; latest.contextId = 1; latest.pinyin = "latest";
        worker.submit(latest, complete);
        CHECK(oldToken->load());
        CHECK(pendingToken->load());
        release.set_value();
        CHECK(done.get_future().wait_for(2s) == std::future_status::ready);
        worker.stop();
        CHECK((ran == std::vector<std::string>{"old", "other", "latest"}));
        CHECK((completed == std::vector<std::string>{"other", "latest"}));
        // Exceptions become results; the worker remains usable.
        std::promise<std::string> error;
        Worker failing([](const Request &) -> Result { throw std::runtime_error("model failed"); });
        failing.submit(Request{}, [&](Result r) { error.set_value(r.error); });
        auto future = error.get_future();
        CHECK(future.wait_for(2s) == std::future_status::ready);
        CHECK(future.get() == "model failed");
        failing.stop();
        // Shutdown cooperatively cancels a running inference.
        std::promise<void> active;
        Worker cancelling([&](const Request &r) {
            active.set_value();
            while (!r.cancelled->load()) std::this_thread::yield();
            return Result{};
        });
        std::atomic_bool delivered{false};
        cancelling.submit(Request{}, [&](Result) { delivered.store(true); });
        CHECK(active.get_future().wait_for(2s) == std::future_status::ready);
        cancelling.stop();
        CHECK(!delivered.load());
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
