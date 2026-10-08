#pragma once

#include "sdk/project_file.hpp"
#include "sdk/session.hpp"

#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include <functional>
#include <memory>

namespace rl::sdk {

// Why a program failed: the last line it wrote to stderr, why it did not
// start, or its exit code.
QString failureText(const ProcessResult& result);
// The emulator command a kit plan (JSON on stdout) describes.
ProcessSpec specFromPlan(const QJsonObject& plan);
// The document of the project's first enabled workspace tab with `codec`;
// empty when there is none.
QString tabDocument(const ProjectFile& project, const QString& codec);
// A JSON document holding an object; both throw std::runtime_error.
QJsonObject loadJsonObject(const QString& path);
void saveJsonObject(const QString& path, const QJsonObject& value);

// What the game sessions share: a project file, messages under its name,
// the kit's build, the kit steps that add documents to a project, and the
// emulator a kit plan describes, started in the Playtest view (demo
// recordings included). A session sets what differs per game.
class KitSession : public Session {
    Q_OBJECT

public:
    const ProjectFile& project() const { return project_; }
    QString projectPath() const { return project_.path(); }
    QString projectDirectory() const { return project_.directory(); }

    // A message in the bottom panel under `source` (a kind, "build",
    // "playtest").
    void report(Severity severity, const QString& source, const QString& message);

    // When the last ROM was built and where ("14:05 · build/game.nes");
    // empty before the first build.
    QString lastBuild() const { return lastBuild_; }

    // Whether the emulator is recording a demo now.
    bool isRecording() const { return recording_; }
    // Ends a recording as the Stop button does; it is applied as usual.
    void stopRecording();

protected:
    explicit KitSession(QObject* parent = nullptr) : Session(parent) {}

    // What one build runs with; prepareBuild fills it in.
    struct BuildStep {
        bool force = false;           // build even when the plugin knows the ROM is current
        QStringList arguments;        // after `studio build <project>`
        QMap<QString, QString> environment;
        bool upToDate = false;        // set: skip the kit, the last ROM stands
        QByteArray inputs;            // what the plugin wants back in built()
    };
    // Checks and extra arguments before the kit runs; false stops the
    // build with `error`.
    virtual bool prepareBuild(BuildStep& step, QString* error);
    // After the kit built the ROM (`artifact`: its path and size).
    virtual void built(const BuildStep& step, const QJsonObject& artifact);
    // Saves the documents, runs `rltool studio build`, reports; `next` gets
    // whether there is a ROM to play.
    void runBuild(std::function<void(bool)> next, bool force = true);

    struct EmulatorPlan {
        QStringList arguments;        // the kit's plan command; --mame <path> is added
        QString label;                // in Build output while the kit plans
        QString title;                // over the Playtest view
        bool demo = false;            // a demo plan (its failure says so)
        bool force = false;           // passed to runBuild
        // A file (relative to the project) the game script writes once the
        // game is up, named to it in `readyVariable`: the Playtest view
        // covers the start until then.
        QString readyFile;
        QString readyVariable;
        // A demo recording to this file (relative to the project): the kit
        // gets --record <path>; `recorded` gets the path after the run.
        QString recordTo;
        QString recordingMessage;     // reported as the recording starts
        std::function<void(const QString& path)> recorded;
        // The game's colours, a ready condition: changes to the command.
        std::function<void(ProcessSpec&)> prepare;
        // A plan kept from an earlier run instead of asking the kit, and
        // where to keep the kit's.
        std::function<bool(ProcessSpec*)> cached;
        std::function<void(const ProcessSpec&)> remember;
    };
    // Builds the ROM, asks the kit how to start it and starts the emulator
    // in the Playtest view.
    void launchPlanned(EmulatorPlan plan);

    struct UpgradeStep {
        QStringList arguments;        // the kit command that adds documents
        QString label;                // in Build output
        QString source;               // where messages go (the kind that asked)
        QString failure;              // "Adding … failed: %1"
        bool saveFirst = false;       // the kit merges into the saved files
        // Takes the new documents from the reloaded project; may throw.
        std::function<void()> adopt;
        QString done;                 // reported when it worked
        // A document to open afterwards (the one that asked for them).
        QString openKind;
        QString openDocument;
    };
    // Runs a kit step that adds documents to the project, reloads the
    // project file and lets the session adopt them.
    void runUpgrade(UpgradeStep step);

    ProjectFile project_;

private:
    // What the kit answered for a plan it ran right after the build.
    struct BuiltPlan {
        bool ran = false;             // the build ran the plan (else ask for it)
        QJsonObject plan;             // the emulator command it describes
        QString error;                // why planning failed
    };
    // runBuild that may pass the kit a plan command to run in the same
    // process once the ROM is built (`studio build --plan-json`): Play then
    // starts one interpreter, not two. `answer` gets the kit's plan.
    void buildThenPlan(std::function<void(bool)> next, bool force, const QStringList& plan,
                       std::shared_ptr<BuiltPlan> answer);
    // The kit's plan command for `plan` with MAME and, for a recording, the
    // file it goes to (removed first).
    QStringList planCommand(const EmulatorPlan& plan, const QString& mame);
    void startEmulator(const EmulatorPlan& plan, ProcessSpec game);
    // Starts the emulator a kit plan document describes.
    void planned(const EmulatorPlan& plan, const QJsonObject& document);

    QString lastBuild_;
    bool recording_ = false;
};

}  // namespace rl::sdk
