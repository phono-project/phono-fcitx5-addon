#pragma once
#include "config.h"
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/eventdispatcher.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyinime.h>
#include <optional>

namespace fcitx { class Instance; }
namespace phono_fcitx {
struct UpdateSnapshot {
    std::string input;
    std::string selected;
    std::string history;
    size_t cursor = 0;
    size_t selectedLength = 0;
    bool splitting = false;
    bool eligible = false;
    bool operator==(const UpdateSnapshot &) const = default;
};
struct SurroundingSnapshot {
    std::string text;
    size_t cursor = 0;
    size_t anchor = 0;
    bool valid = false;
    bool operator==(const SurroundingSnapshot &) const = default;
};
struct State : fcitx::InputContextProperty {
    State(libime::PinyinIME *ime, uint64_t id) : pinyin(ime), id(id) {}
    ~State() override { if (cancelled) cancelled->store(true); }
    void clearPendingHistory() { pendingHistory.reset(); pendingSurrounding.reset(); pendingAcknowledged.reset(); pendingPrefixes.clear(); }
    void invalidate() {
        if (cancelled) cancelled->store(true);
        cancelled.reset();
        ++generation;
        beams.clear();
        invalid.clear();
        error.clear();
        lastUpdate.reset();
    }
    libime::PinyinContext pinyin;
    const uint64_t id;
    uint64_t generation = 0;
    bool splitting = false;
    Cancellation cancelled;
    std::vector<std::string> beams;
    std::vector<InvalidInput> invalid;
    std::string error;
    std::string localHistory;
    // Until the frontend acknowledges all outstanding commits, account for text
    // we have just committed without changing Fcitx's shared surrounding cache.
    std::optional<std::string> pendingHistory;
    // Predicted document after all commits; detects moving back into that
    // document instead of mistaking the old prefix for a delayed confirmation.
    std::optional<SurroundingSnapshot> pendingSurrounding;
    // Last application snapshot, including a partial commit confirmation.
    std::optional<SurroundingSnapshot> pendingAcknowledged;
    // Prefix after each outstanding commit, including its initial base. An
    // application may acknowledge several commits in separate notifications.
    std::vector<std::string> pendingPrefixes;
    std::optional<UpdateSnapshot> lastUpdate;
};
class Engine : public fcitx::InputMethodEngineV3 {
public:
    explicit Engine(fcitx::Instance *, Worker::Inference inference = {});
    ~Engine() override;
    void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &) override;
    void reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &) override;
    void deactivate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &) override;
    void activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &) override;
    void invokeActionImpl(const fcitx::InputMethodEntry &, fcitx::InvokeActionEvent &) override;
    const fcitx::Configuration *getConfig() const override { return &config_; }
    void setConfig(const fcitx::RawConfig &) override;
    void reloadConfig() override;
    void save() override;
    void select(fcitx::InputContext *, uint64_t generation, bool model, size_t index);
    State *state(fcitx::InputContext *ic);
private:
    void update(fcitx::InputContext *, bool infer);
    void render(fcitx::InputContext *);
    void commit(fcitx::InputContext *, const std::string &, bool learn);
    bool punctuation(fcitx::InputContext *, const fcitx::Key &);
    std::string history(fcitx::InputContext *) const;
    void recordCommitHistory(fcitx::InputContext *, const std::string &prior, const std::string &text);
    bool sensitive(fcitx::InputContext *) const;
    void configChanged();
    fcitx::Instance *instance_;
    Config config_;
    uint64_t nextContext_ = 0;
    std::unique_ptr<libime::PinyinIME> ime_;
    Core core_;
    fcitx::EventDispatcher dispatcher_;
    Worker worker_;
    fcitx::FactoryFor<State> factory_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>> watchers_;
};
} // namespace phono_fcitx
