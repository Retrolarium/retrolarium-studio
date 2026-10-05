#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::doraemon {

// Adds the Doraemon plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::doraemon
