#include "castle/plugin/castle_plugin.hpp"

#include "castle/plugin/castle_session.hpp"
#include "sdk/plugin.hpp"

namespace rl::castle {

namespace {

// Opens projects created by `rltool studio preset` from a Castle ROM.
class CastlePlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("castle-excellent"); }
    QString displayName() const override { return QStringLiteral("Castle Excellent"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        const QString mode = project.string(QStringLiteral("project.source_mode"));
        return project.string(QStringLiteral("project.profile")) == QLatin1String("castle-excellent.v1") &&
               (mode == QLatin1String("binary-profile") || mode == QLatin1String("rom"));
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return CastleSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<CastlePlugin>());
}

}  // namespace rl::castle
