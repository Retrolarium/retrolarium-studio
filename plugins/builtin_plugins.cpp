#include "builtin_plugins.hpp"

#include "flicky/plugin/flicky_plugin.hpp"
#include "sdk/plugin.hpp"

namespace rl::plugins {

// The one place that names concrete plugins. Add a line per plugin.
void registerBuiltinPlugins() {
    sdk::PluginRegistry& registry = sdk::PluginRegistry::instance();
    flicky::registerPlugin(registry);
}

}  // namespace rl::plugins
