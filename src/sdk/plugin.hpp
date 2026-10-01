#pragma once

#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <vector>

class QWidget;

namespace rl::sdk {

// Services the studio offers to plugins.
class Host {
public:
    virtual ~Host() = default;

    // Parent for dialogs a plugin opens (file pickers, confirmations).
    virtual QWidget* dialogParent() const = 0;
    // Opens a project file, e.g. one a plugin action just created.
    virtual void openProject(const QString& path) = 0;
    // Adds a message to the bottom panel.
    virtual void report(const Diagnostic& diagnostic) = 0;
    // Appends a line to the Build output panel.
    virtual void log(const QString& line) = 0;
};

// A plugin command in the Project menu, e.g. "Import Flicky (UE) from ROM…".
struct ProjectAction {
    QString id;
    QString text;
    std::function<void(Host&)> run;
};

// A game or platform plugin. Plugins are linked into the studio and
// registered at startup; the studio core never includes their headers.
class Plugin {
public:
    virtual ~Plugin() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    virtual bool canOpen(const ProjectFile& project) const = 0;
    virtual std::unique_ptr<Session> open(const ProjectFile& project, QString* error) = 0;

    virtual QVector<ProjectAction> projectActions() const { return {}; }
};

class PluginRegistry {
public:
    static PluginRegistry& instance();

    void add(std::unique_ptr<Plugin> plugin);
    const std::vector<std::unique_ptr<Plugin>>& plugins() const { return plugins_; }
    Plugin* pluginFor(const ProjectFile& project) const;

private:
    std::vector<std::unique_ptr<Plugin>> plugins_;
};

}  // namespace rl::sdk
