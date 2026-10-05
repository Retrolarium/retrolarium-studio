#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::pacman {

// Adds the Pac-Man (NES) plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::pacman
