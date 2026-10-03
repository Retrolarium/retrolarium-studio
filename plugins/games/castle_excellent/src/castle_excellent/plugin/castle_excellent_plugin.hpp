#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::castle {

// Adds the Castle Excellent / Castlequest plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::castle
