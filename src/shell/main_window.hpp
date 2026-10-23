#pragma once

#include "frame/studio_window.hpp"
#include "sdk/plugin.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include <memory>

class QAction;
class QActionGroup;
class QJsonObject;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class QHBoxLayout;

namespace rl::editors {
class EditorView;
}

namespace rl::runtime {
class AudioPlayer;
class EmulatorRunner;
class ProcessRunner;
}  // namespace rl::runtime

namespace rl::ui {
class Badge;
class BudgetBar;
class Label;
class PrimaryButton;
class Toast;
}  // namespace rl::ui

namespace rl::shell {

class BottomPanel;
class DocumentPanel;
class InspectorPanel;
class KindRail;

// The studio: global toolbar, kind rail, document list, editor, inspector and
// bottom panel around one open project session. Game knowledge comes only from
// the session a plugin returns.
class MainWindow final : public frame::StudioWindow, public sdk::Host {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // prepared: the kit has just got a game version's project ready.
    bool openProjectFile(const QString& path, bool prepared = false);
    bool hasProject() const { return session_ != nullptr; }
    // Navigates to a document, as if picked in the rail and document list.
    void showDocument(const QString& kind, const QString& document);

    // sdk::Host
    QWidget* dialogParent() const override;
    void openProject(const QString& path) override;
    void report(const sdk::Diagnostic& diagnostic) override;
    void log(const QString& line) override;
    void runProcess(const sdk::ProcessSpec& spec, std::function<void(const sdk::ProcessResult&)> done) override;
    bool isBusy() const override;
    void cancel() override;
    sdk::ProcessSpec kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                QString* error) const override;
    QString toolPath(const QString& name) const override;
    QString emulatorPath(const QString& id) const override;
    void launchEmulator(const sdk::ProcessSpec& spec, const QString& title,
                        std::function<void(const sdk::ProcessResult&)> finished) override;
    void stopEmulator() override;
    void playAudio(const QString& wavPath) override;
    void stopAudio() override;
    bool audioLoops() const override;
    void setAudioLoop(bool loop) override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    QWidget* buildToolbar();
    QWidget* buildCenter();
    QWidget* buildStatusBar();
    QWidget* buildWelcome();
    // The running kit task on screen: its name and how long it has run, in
    // the status bar and on the welcome page, whose buttons wait for it.
    void showTask(const QString& label);
    void tickTask();
    void hideTask();

    void closeProject();
    bool confirmDiscard(bool remembered = true);
    // Asks about the TAS editor's edits not saved; false: cancelled.
    bool confirmMovieEdits();
    // The session and undo position the user last chose to discard: asking
    // again before closing it would only repeat the question.
    const sdk::Session* discardedSession_ = nullptr;
    int discardedIndex_ = -1;
    void selectKind(const QString& kind);
    void selectDocument(const QString& document);
    void installEditor();
    void updateToolbarRow();
    void updateProjectChrome();
    void updateBreadcrumb();
    void updateDirtyState();
    // Runs updateDirtyState once the current event is handled: an edit
    // reports every kind's documents, and each report would rescan all.
    void scheduleDirtyState();
    void updateStatus(const QString& transient = QString());
    void refreshDiagnostics();
    void saveAll();
    void revertDocument();
    void chooseProject();
    void rememberRecent(const QString& path);
    // Restarts into the language just chosen, reopening the project.
    void restartStudio();
    // An edit the editor or the inspector refused: why, where the user looks.
    void refuseAction(const QString& message);
    void rebuildRecentMenu();
    // A game repository's .rlj (schema retrolarium.game): pick a revision,
    // let the kit ready it (its ROM, the asset split, hack/) and open the
    // project it generates.
    void openGameRepository(const QString& descriptor);
    // Open ROM: the kit finds the ROM's game, downloads its repository when
    // none is on disk yet, and readies the version the ROM is.
    void openRom();
    void openRomFile(const QString& rom);
    // A known game's ROM: its repository, downloaded and set up. Any other
    // ROM (or a known one the user opens as it is): a ROM project.
    void setUpGameSource(const QString& rom);
    void openRomAsProject(const QString& rom);
    void rememberRepository(const QString& descriptor);
    // project: the version's project being opened by its own file, which
    // opens as it is when the kit cannot get it ready.
    void prepareRevision(const QString& descriptor, const QString& revision, const QString& rom,
                         const QString& project = QString());
    // The repository .rlj of a generated revision project, or empty.
    QString currentDescriptor() const;
    // Discards the open version's edits through the kit, then reopens it.
    void resetRevision();
    void createPatch();
    void updateActions();
    void togglePlaytestPause();
    // Run > Speed: a step of kSpeeds, or the next one up or down.
    void setEmulationSpeed(int percent);
    void stepEmulationSpeed(int direction);
    void stepStateSlot(int direction);
    void notifyEmulation(const QString& text);
    // Run > Movie: asks the kit how this version runs from power-on and what
    // movies it has, then records, resumes or plays one.
    void startMovie(int mode);
    void launchMovie(const QJsonObject& answer, int mode, const QString& file, const QString& header, int stopAt,
                     bool closeAtEnd, const QString& origin = QString());
    // Run > Movie > TAS editor: on the movie that plays, or on one chosen
    // to play first.
    void openTasEditor();
    void closeTasEditor();
    // The TAS editor is on screen: the Edit menu, Save, Pause and Stop work
    // on its movie.
    bool tasEditorShown() const;
    void showEmulationEvent(const QString& event);
    void showPlaytest();
    QString emulatorKind() const;
    void releaseEditor();

    std::unique_ptr<sdk::Session> session_;
    std::unique_ptr<sdk::EditorModel> model_;
    std::unique_ptr<editors::EditorView> editor_;
    // The Playtest editor stays alive while other kinds are shown, so a
    // running game keeps its window.
    std::unique_ptr<sdk::EditorModel> parkedModel_;
    std::unique_ptr<editors::EditorView> parkedEditor_;
    QString parkedDocument_;
    runtime::ProcessRunner* processes_ = nullptr;
    runtime::EmulatorRunner* emulator_ = nullptr;
    runtime::AudioPlayer* audio_ = nullptr;
    QTimer* dirtyTimer_ = nullptr;
    QString projectPath_;
    QString kind_;
    QString document_;
    QHash<QString, QString> lastDocument_;  // kind -> document
    QVector<sdk::Diagnostic> hostMessages_;

    // Toolbar.
    QPushButton* projectButton_ = nullptr;
    ui::Badge* platformBadge_ = nullptr;
    ui::Label* projectName_ = nullptr;
    ui::Badge* revisionBadge_ = nullptr;
    QWidget* dirtyIndicator_ = nullptr;
    ui::Label* dirtyLabel_ = nullptr;
    QToolButton* undoButton_ = nullptr;
    QToolButton* redoButton_ = nullptr;
    QToolButton* saveButton_ = nullptr;
    QToolButton* revertButton_ = nullptr;
    QPushButton* buildButton_ = nullptr;
    QPushButton* playButton_ = nullptr;
    ui::PrimaryButton* playSelectedButton_ = nullptr;
    QToolButton* pauseButton_ = nullptr;
    QToolButton* stopButton_ = nullptr;

    // Body.
    KindRail* rail_ = nullptr;
    DocumentPanel* documents_ = nullptr;
    QStackedWidget* centerStack_ = nullptr;
    QWidget* welcome_ = nullptr;
    QWidget* workspace_ = nullptr;
    ui::Label* crumbKind_ = nullptr;
    ui::Label* crumbDocument_ = nullptr;
    ui::Label* crumbEdited_ = nullptr;
    ui::Badge* crumbEditor_ = nullptr;
    ui::Label* budgetText_ = nullptr;
    ui::BudgetBar* budgetBar_ = nullptr;
    QWidget* toolbarRow_ = nullptr;
    QHBoxLayout* toolbarRowLayout_ = nullptr;
    QWidget* toolbarContent_ = nullptr;
    QStackedWidget* editorStack_ = nullptr;
    ui::Toast* toast_ = nullptr;
    QWidget* emptyEditor_ = nullptr;
    ui::Label* emptyTitle_ = nullptr;
    ui::Label* emptyNote_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    BottomPanel* bottom_ = nullptr;

    // Status bar.
    ui::Label* statusLeft_ = nullptr;
    QWidget* taskStatus_ = nullptr;
    ui::Label* taskStatusLabel_ = nullptr;
    QWidget* welcomeTask_ = nullptr;
    ui::Label* welcomeTaskLabel_ = nullptr;
    QVector<QWidget*> welcomeButtons_;
    QTimer* taskTimer_ = nullptr;
    QElapsedTimer taskClock_;
    QString taskLabel_;
    QPushButton* statusSummary_ = nullptr;
    ui::Label* statusRight_ = nullptr;

    // Actions.
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* cutFrameAction_ = nullptr;
    QAction* copyFrameAction_ = nullptr;
    QAction* pasteFrameAction_ = nullptr;
    QAction* insertFrameAction_ = nullptr;
    QAction* deleteFrameAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* closeAction_ = nullptr;
    QAction* switchRevisionAction_ = nullptr;
    QAction* patchAction_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* buildAction_ = nullptr;
    QAction* playAction_ = nullptr;
    QAction* playSelectedAction_ = nullptr;
    QAction* pauseGameAction_ = nullptr;
    QAction* stopGameAction_ = nullptr;
    QAction* frameAdvanceAction_ = nullptr;
    QAction* rewindAction_ = nullptr;
    QAction* rewindSecondAction_ = nullptr;
    QAction* showInputAction_ = nullptr;
    QAction* showWatchAction_ = nullptr;
    QMenu* speedMenu_ = nullptr;
    QActionGroup* speedGroup_ = nullptr;
    QAction* slowerAction_ = nullptr;
    QAction* fasterAction_ = nullptr;
    QAction* saveStateAction_ = nullptr;
    QAction* loadStateAction_ = nullptr;
    QAction* previousSlotAction_ = nullptr;
    QAction* nextSlotAction_ = nullptr;
    QAction* recordMovieAction_ = nullptr;
    QAction* resumeMovieAction_ = nullptr;
    QAction* playMovieAction_ = nullptr;
    QAction* stopMovieAction_ = nullptr;
    QAction* tasEditorAction_ = nullptr;
    class TasEditorWindow* tasEditor_ = nullptr;
    bool openEditorOnLaunch_ = false;
    QMenu* recentMenu_ = nullptr;
};

}  // namespace rl::shell
