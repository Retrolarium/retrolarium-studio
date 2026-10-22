#include "pacman/plugin/pacman_plugin.hpp"

#include "pacman/plugin/pacman_session.hpp"
#include "sdk/plugin.hpp"

namespace rl::pacman {

namespace {

// Opens projects created by `rldisasm preset` from a Pac-Man ROM.
class PacmanPlugin final : public sdk::Plugin {
public:
    QString id() const override { return QStringLiteral("pacman"); }
    QString displayName() const override { return QStringLiteral("Pac-Man"); }

    bool canOpen(const sdk::ProjectFile& project) const override {
        // A preset project, or one the kit generates from the repository's
        // pacman.rlj for a revision.
        const QString mode = project.string(QStringLiteral("project.source_mode"));
        return project.string(QStringLiteral("project.profile")) == QLatin1String("pacman.v1") &&
               (mode == QLatin1String("binary-profile") || mode == QLatin1String("rom"));
    }

    std::unique_ptr<sdk::Session> open(const sdk::ProjectFile& project, QString* error) override {
        return PacmanSession::open(project, error);
    }
};

}  // namespace

void registerPlugin(sdk::PluginRegistry& registry) { registry.add(std::make_unique<PacmanPlugin>()); }

}  // namespace rl::pacman
