#include "solomon/plugin/solomon_plugin.hpp"

#include "sdk/i18n.hpp"
#include "sdk/plugin.hpp"
#include "solomon/plugin/solomon_session.hpp"

namespace rl::solomon {

namespace {

// Opens projects created by `rldisasm preset` from a Solomon's Key ROM.
class SolomonPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("solomons-key"); }
    QString displayName() const override { return rlTr("solomon", "Solomon's Key"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        const QString mode = project.string(QStringLiteral("project.source_mode"));
        return project.string(QStringLiteral("project.profile")) == QLatin1String("solomons-key.v1") &&
               (mode == QLatin1String("binary-profile") || mode == QLatin1String("rom"));
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return SolomonSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) { registry.add(std::make_unique<SolomonPlugin>()); }

}  // namespace rl::solomon
