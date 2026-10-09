#include "builtin_plugins.hpp"

#include "castle_excellent/plugin/castle_excellent_plugin.hpp"
#include "doraemon/plugin/doraemon_plugin.hpp"
#include "flicky/plugin/flicky_plugin.hpp"
#include "pacman/plugin/pacman_plugin.hpp"
#include "smb/plugin/smb_plugin.hpp"
#include "solomon/plugin/solomon_plugin.hpp"
#include "sdk/plugin.hpp"

namespace rl::plugins {

// The one place that names concrete plugins. Add a line per plugin.
void registerBuiltinPlugins() {
    sdk::PluginRegistry& registry = sdk::PluginRegistry::instance();
    castle_excellent::registerPlugin(registry);
    doraemon::registerPlugin(registry);
    flicky::registerPlugin(registry);
    pacman::registerPlugin(registry);
    smb::registerPlugin(registry);
    solomon::registerPlugin(registry);
}

}  // namespace rl::plugins
