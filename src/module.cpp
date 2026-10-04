// Pull the factory-containing translation unit from the engine archive.
#include "addon.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
namespace phono_fcitx {
class ModuleFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new Engine(manager->instance());
    }
};
}
FCITX_ADDON_FACTORY(phono_fcitx::ModuleFactory)
