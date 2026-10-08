#include "doraemon/plugin/doraemon_plugin.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "sdk/plugin.hpp"

namespace rl::doraemon {

namespace {

// Opens projects created by `rldisasm preset` from a Doraemon ROM.
class DoraemonPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("doraemon"); }
    QString displayName() const override { return QStringLiteral("Doraemon"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        const QString mode = project.string(QStringLiteral("project.source_mode"));
        return project.string(QStringLiteral("project.profile")) == QLatin1String("doraemon.v1") &&
               (mode == QLatin1String("binary-profile") || mode == QLatin1String("rom"));
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return DoraemonSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<DoraemonPlugin>());
}

}  // namespace rl::doraemon
