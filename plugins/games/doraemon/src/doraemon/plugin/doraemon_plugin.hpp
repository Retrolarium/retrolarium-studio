#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::flicky {

// Adds the Flicky (UE) plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::flicky
