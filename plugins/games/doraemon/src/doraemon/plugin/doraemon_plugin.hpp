#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::smb {

// Adds the Super Mario Bros. plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::smb
