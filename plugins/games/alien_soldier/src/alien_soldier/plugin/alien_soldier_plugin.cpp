#include "alien_soldier/plugin/alien_soldier_plugin.hpp"

#include "alien_soldier/plugin/session.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"

namespace rl::alien_soldier {

namespace {

// Opens the source-built projects the kit prepares from the disassembly.
class AlienSoldierPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("alien_soldier"); }
    QString displayName() const override { return rlTr("alien_soldier", "Alien Soldier"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        return project.string(QStringLiteral("project.profile")) == QLatin1String("alien-soldier.v1") &&
               project.string(QStringLiteral("project.source_mode")) == QLatin1String("source");
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return AlienSoldierSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) { registry.add(std::make_unique<AlienSoldierPlugin>()); }

}  // namespace rl::alien_soldier
