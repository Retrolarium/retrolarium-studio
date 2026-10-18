#include "rom/rom_plugin.hpp"

#include "rom/rom_session.hpp"
#include "sdk/plugin.hpp"

namespace rl::rom {

namespace {

// Opens the projects the studio makes for a ROM it has no game for.
class RomPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("rom"); }
    QString displayName() const override { return QStringLiteral("ROM"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        return project.string(QStringLiteral("project.profile")) == QLatin1String("rom.v1");
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return RomSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<RomPlugin>());
}

}  // namespace rl::rom
