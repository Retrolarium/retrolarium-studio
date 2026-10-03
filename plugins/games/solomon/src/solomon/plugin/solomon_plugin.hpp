#pragma once

namespace rl::sdk {
class PluginRegistry;
}

namespace rl::solomon {

// Adds the Solomon's Key plugin to the studio's registry.
void registerPlugin(sdk::PluginRegistry& registry);

}  // namespace rl::solomon
