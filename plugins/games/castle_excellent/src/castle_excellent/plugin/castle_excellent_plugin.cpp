#include "castle_excellent/plugin/castle_excellent_plugin.hpp"

#include "castle_excellent/plugin/castle_excellent_session.hpp"
#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"

namespace rl::castle_excellent {

namespace {

// Opens projects created by `rldisasm preset` from a Castle Excellent ROM.
class CastleExcellentPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("castle-excellent"); }
    QString displayName() const override { return rlTr("castle_excellent", "Castle Excellent"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        const QString mode = project.string(QStringLiteral("project.source_mode"));
        return project.string(QStringLiteral("project.profile")) == QLatin1String("castle-excellent.v1") &&
               (mode == QLatin1String("binary-profile") || mode == QLatin1String("rom"));
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return CastleExcellentSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) { registry.add(std::make_unique<CastleExcellentPlugin>()); }

}  // namespace rl::castle_excellent
