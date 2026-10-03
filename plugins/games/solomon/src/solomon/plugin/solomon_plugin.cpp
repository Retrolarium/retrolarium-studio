#include "solomon/plugin/solomon_plugin.hpp"

#include "sdk/plugin.hpp"
#include "solomon/plugin/solomon_session.hpp"

namespace rl::solomon {

namespace {

// Opens projects created by `rltool studio preset` from a Solomon's Key ROM.
class SolomonPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("solomons-key"); }
    QString displayName() const override { return QStringLiteral("Solomon's Key"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        return project.string(QStringLiteral("project.profile")) == QLatin1String("solomons-key.v1") &&
               project.string(QStringLiteral("project.source_mode")) == QLatin1String("binary-profile");
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return SolomonSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<SolomonPlugin>());
}

}  // namespace rl::solomon
