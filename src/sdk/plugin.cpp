#include "sdk/plugin.hpp"

namespace rl::sdk {

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry registry;
    return registry;
}

void PluginRegistry::add(std::unique_ptr<Plugin> plugin) {
    if (plugin) {
        plugins_.push_back(std::move(plugin));
    }
}

Plugin* PluginRegistry::pluginFor(const ProjectFile& project) const {
    for (const auto& plugin : plugins_) {
        if (plugin->canOpen(project)) {
            return plugin.get();
        }
    }
    return nullptr;
}

}  // namespace rl::sdk
