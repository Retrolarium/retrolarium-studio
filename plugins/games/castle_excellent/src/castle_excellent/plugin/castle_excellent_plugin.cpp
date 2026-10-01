#include "smb/plugin/smb_plugin.hpp"

#include "sdk/plugin.hpp"
#include "smb/plugin/smb_session.hpp"

namespace rl::smb {

namespace {

// Opens projects created by `rltool studio preset` from an SMB1 ROM.
class SmbPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("smb1"); }
    QString displayName() const override { return QStringLiteral("Super Mario Bros."); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        return project.string(QStringLiteral("project.profile")) == QLatin1String("smb1.v1") &&
               project.string(QStringLiteral("project.source_mode")) == QLatin1String("binary-profile");
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return SmbSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) {
    registry.add(std::make_unique<SmbPlugin>());
}

}  // namespace rl::smb
