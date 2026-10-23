#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::alien_soldier {

// Adds the Alien Soldier plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::alien_soldier
