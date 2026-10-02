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

    // Runs a program asynchronously; output streams to Build output. Only
    // one program runs at a time; `done` is called exactly once.
    virtual void runProcess(const ProcessSpec& spec, std::function<void(const ProcessResult&)> done) = 0;
    virtual bool isBusy() const = 0;
    virtual void cancel() = 0;

    // A Retrolarium Kit invocation (rltool or python rltool.py) with `arguments`.
    // `error` explains why when the kit cannot be found.
    virtual ProcessSpec kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                   QString* error) const = 0;
    // A helper executable shipped next to the studio, e.g. a level writer.
    virtual QString toolPath(const QString& name) const = 0;
    // An emulator executable by id ("mame"); empty when not found.
    virtual QString emulatorPath(const QString& id) const = 0;

    // Starts an emulator embedded in the Playtest view. `finished` runs when
    // it exits. The host adds its own window-embedding arguments.
    virtual void launchEmulator(const ProcessSpec& spec, const QString& title,
                                std::function<void(const ProcessResult&)> finished) = 0;

    // Plays a WAV file (sound previews); a new call replaces the last one.
    virtual void playAudio(const QString& wavPath) = 0;
    virtual void stopAudio() = 0;
    // Whether previews repeat until stopped. Changing it while a sound
    // plays takes effect at the end of the current pass.
    virtual bool audioLoops() const = 0;
    virtual void setAudioLoop(bool loop) = 0;
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
