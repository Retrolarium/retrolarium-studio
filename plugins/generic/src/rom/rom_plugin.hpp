#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::rom {

// Adds the plugin for ROMs no game plugin knows (profile rom.v1).
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::rom
