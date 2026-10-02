#pragma once

#include "frame/studio_window.hpp"
#include "sdk/plugin.hpp"

#include <QHash>
#include <QString>

#include <memory>

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QHBoxLayout;

namespace rl::editors {
class EditorView;
}

namespace rl::runtime {
class AudioPlayer;
class EmulatorRunner;
class ProcessRunner;
}

namespace rl::ui {
class Badge;
class BudgetBar;
class Label;
class PrimaryButton;
class Toast;
}

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

    bool openProjectFile(const QString& path);
    bool hasProject() const { return session_ != nullptr; }
    // Navigates to a document, as if picked in the rail and document list.
    void showDocument(const QString& kind, const QString& document);

    // sdk::Host
    QWidget* dialogParent() const override;
    void openProject(const QString& path) override;
    void report(const sdk::Diagnostic& diagnostic) override;
    void log(const QString& line) override;
    void runProcess(const sdk::ProcessSpec& spec,
                    std::function<void(const sdk::ProcessResult&)> done) override;
    bool isBusy() const override;
    void cancel() override;
    sdk::ProcessSpec kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                QString* error) const override;
    QString toolPath(const QString& name) const override;
    QString emulatorPath(const QString& id) const override;
    void launchEmulator(const sdk::ProcessSpec& spec, const QString& title,
                        std::function<void(const sdk::ProcessResult&)> finished) override;
    void playAudio(const QString& wavPath) override;
    void stopAudio() override;
    bool audioLoops() const override;
    void setAudioLoop(bool loop) override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    QWidget* buildToolbar();
    QWidget* buildCenter();
    QWidget* buildStatusBar();
    QWidget* buildWelcome();

    void closeProject();
    bool confirmDiscard();
    void selectKind(const QString& kind);
    void selectDocument(const QString& document);
    void installEditor();
    void updateToolbarRow();
    void updateProjectChrome();
    void updateBreadcrumb();
    void updateDirtyState();
    void updateStatus(const QString& transient = QString());
    void refreshDiagnostics();
    void saveAll();
    void revertDocument();
    void chooseProject();
    void rememberRecent(const QString& path);
    void rebuildRecentMenu();
    void updateActions();
    void togglePlaytestPause();
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
    QPushButton* statusSummary_ = nullptr;
    ui::Label* statusRight_ = nullptr;

    // Actions.
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* closeAction_ = nullptr;
    QAction* buildAction_ = nullptr;
    QAction* playAction_ = nullptr;
    QAction* playSelectedAction_ = nullptr;
    QAction* pauseGameAction_ = nullptr;
    QAction* stopGameAction_ = nullptr;
    QMenu* recentMenu_ = nullptr;
};

}  // namespace rl::shell
