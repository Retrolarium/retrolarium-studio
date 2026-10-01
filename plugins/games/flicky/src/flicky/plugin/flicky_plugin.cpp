#include "flicky/plugin/flicky_plugin.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "sdk/plugin.hpp"

namespace rl::flicky {

namespace {

// Opens content projects created by `rltool studio flicky-create`.
class FlickyPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("flicky"); }
    QString displayName() const override { return QStringLiteral("Flicky (UE)"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        return project.string(QStringLiteral("project.profile")) == QLatin1String("flicky-ue.v1") &&
               project.string(QStringLiteral("project.source_mode")) == QLatin1String("flicky-content");
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return FlickySession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<FlickyPlugin>());
}

}  // namespace rl::flicky
