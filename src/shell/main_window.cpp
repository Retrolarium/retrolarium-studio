#include "shell/main_window.hpp"

#include "editors/editor_view.hpp"
#include "editors/emulator_view.hpp"
#include "editors/mime.hpp"
#include "frame/studio_dialog.hpp"
#include "frame/title_bar.hpp"
#include "runtime/audio_player.hpp"
#include "runtime/emulator_runner.hpp"
#include "runtime/process_runner.hpp"
#include "runtime/tool_locator.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"
#include "sdk/kit_session.hpp"
#include "nes/palette.hpp"
#include "shell/about_dialog.hpp"
#include "shell/bottom_panel.hpp"
#include "shell/inspector_panel.hpp"
#include "shell/movie_dialogs.hpp"
#include "shell/tas_editor.hpp"
#include "shell/project_links.hpp"
#include "shell/revision_dialog.hpp"
#include "shell/settings_dialog.hpp"
#include "shell/side_panels.hpp"
#include "theme/icons.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/badge.hpp"
#include "widgets/brand_mark.hpp"
#include "widgets/budget_bar.hpp"
#include "widgets/label.hpp"
#include "widgets/toast.hpp"
#include "widgets/primary_button.hpp"
#include "widgets/view_chip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <iterator>

namespace rl::shell {

namespace {

constexpr int kMaxRecent = 8;
// Run > Speed, in percent (0: no limit).
constexpr int kSpeeds[] = {10, 25, 50, 75, 100, 150, 200, 300, 400, 0};
const char* kRecentKey = "recentProjects";
// The game repositories (.rlj descriptors) opened so far: Open ROM looks
// in them before it downloads one.
const char* kRepositoriesKey = "gameRepositories";

QFrame* surface(const char* name, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setProperty("surface", QString::fromLatin1(name));
    frame->setFrameShape(QFrame::NoFrame);
    return frame;
}

QToolButton* toolbarButton(const QString& icon, const QString& tip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setIcon(theme::icon(icon));
    button->setIconSize(QSize(17, 17));
    button->setFixedSize(30, 30);
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// What Recent shows: the project's name ("Pac-Man [Japan · v1.0]").
QString recentLabel(const QString& path) {
    QString error;
    const sdk::ProjectFile project = sdk::ProjectFile::load(path, &error);
    QString name = project.isValid() ? project.string(QStringLiteral("project.name")) : QString();
    if (name.isEmpty() && project.isValid()) name = project.string(QStringLiteral("game.name"));
    return name.isEmpty() ? QFileInfo(path).fileName() : name;
}

// Where downloaded games and ROM projects go: the user's Documents, or the
// test run's own home (RETROLARIUM_STUDIO_HOME, tools/autotest).
QString documentsDirectory() {
    const QString home = qEnvironmentVariable("RETROLARIUM_STUDIO_HOME");
    return home.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                          : QDir(home).filePath(QStringLiteral("Documents"));
}

// The repository .rlj of a version's project
// (<repository>/.retrolarium/<version>/<game>-<version>.rlj), or empty.
QString repositoryDescriptor(const QString& projectPath) {
    QDir repository = QFileInfo(projectPath).absoluteDir();
    if (!repository.cdUp() || repository.dirName() != QLatin1String(".retrolarium") || !repository.cdUp()) return {};
    QString error;
    for (const QFileInfo& info : repository.entryInfoList({QStringLiteral("*.rlj")}, QDir::Files)) {
        const sdk::ProjectFile candidate = sdk::ProjectFile::load(info.absoluteFilePath(), &error);
        if (candidate.string(QStringLiteral("schema")) == QLatin1String("retrolarium.game"))
            return info.absoluteFilePath();
    }
    return {};
}

// MAME lines Messages keeps.
constexpr int kEmulatorMessages = 200;

sdk::Diagnostic message(sdk::Severity severity, const QString& source, const QString& text, const QString& location) {
    sdk::Diagnostic d;
    d.severity = severity;
    d.source = source;
    d.message = text;
    d.location = location;
    return d;
}

// A filled 6px accent square, the "unsaved" mark of the toolbar.
class DirtyMark final : public QWidget {
public:
    explicit DirtyMark(QWidget* parent) : QWidget(parent) { setFixedSize(6, 6); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), theme::current().accent);
    }
};

// The editor toolbar's row: as wide as the row allows, never narrower than
// its controls need; past that it scrolls sideways with the wheel and shows
// no bar, so the row keeps its height.
class ToolbarScroller final : public QScrollArea {
public:
    explicit ToolbarScroller(QWidget* parent) : QScrollArea(parent) {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const QPoint delta = event->angleDelta();
        QScrollBar* bar = horizontalScrollBar();
        bar->setValue(bar->value() - (delta.x() != 0 ? delta.x() : delta.y()) / 2);
        event->accept();
    }
};

// The RAM cells a game repository's movie checks read (movies/*.check.json),
// for the watch overlay: main CPU cells, the first of each name.
QVector<runtime::WatchCell> watchCells(const QString& repository) {
    QVector<runtime::WatchCell> cells;
    QSet<QString> names;
    const QDir movies(QDir(repository).filePath(QStringLiteral("movies")));
    for (const QFileInfo& info : movies.entryInfoList({QStringLiteral("*.check.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject all =
            QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("cells")).toObject();
        for (auto it = all.begin(); it != all.end(); ++it) {
            const QJsonObject cell = it.value().toObject();
            const QString space = cell.value(QStringLiteral("space")).toString(QStringLiteral("program"));
            bool ok = false;
            const quint32 address = cell.value(QStringLiteral("address")).toString().toUInt(&ok, 0);
            if (!ok || space != QLatin1String("program") || names.contains(it.key())) continue;
            names.insert(it.key());
            cells.append({it.key(), address, qBound(1, cell.value(QStringLiteral("length")).toInt(1), 8)});
        }
    }
    return cells;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : StudioWindow(parent) {
    processes_ = new runtime::ProcessRunner(this);
    emulator_ = new runtime::EmulatorRunner(this);
    audio_ = new runtime::AudioPlayer(this);
    setWindowTitle(tr("Retrolarium"));
    titleBar()->setTitle(tr("Retrolarium Studio"));
    setMinimumSize(1280, 780);
    // A ROM or a project dropped on the window opens (see dropEvent).
    setAcceptDrops(true);
    buildMenus();

    auto* root = surface("chrome", this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildToolbar());
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, root));

    auto* body = new QWidget(root);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    rail_ = new KindRail(body);
    documents_ = new DocumentPanel(body);
    inspector_ = new InspectorPanel(body);
    inspector_->setFixedWidth(300);
    bodyLayout->addWidget(rail_);
    bodyLayout->addWidget(new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, body));
    bodyLayout->addWidget(documents_);
    bodyLayout->addWidget(new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, body));
    bodyLayout->addWidget(buildCenter(), 1);
    bodyLayout->addWidget(new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, body));
    bodyLayout->addWidget(inspector_);
    layout->addWidget(body, 1);

    bottom_ = new BottomPanel(root);
    layout->addWidget(bottom_);
    layout->addWidget(buildStatusBar());
    setCentralWidget(root);

    connect(rail_, &KindRail::kindChosen, this, &MainWindow::selectKind);
    connect(documents_, &DocumentPanel::documentChosen, this, &MainWindow::selectDocument);
    connect(inspector_, &InspectorPanel::fieldRejected, this, &MainWindow::refuseAction);
    connect(bottom_, &BottomPanel::summaryChanged, this,
            [this](const QString& summary) { statusSummary_->setText(summary); });
    connect(bottom_, &BottomPanel::diagnosticActivated, this, [this](const sdk::Diagnostic& d) {
        if (!session_ || d.kind.isEmpty()) {
            return;
        }
        selectKind(d.kind);
        if (!d.document.isEmpty()) {
            documents_->selectDocument(d.document);
            selectDocument(d.document);
        }
    });

    taskTimer_ = new QTimer(this);
    taskTimer_->setInterval(1000);
    connect(taskTimer_, &QTimer::timeout, this, &MainWindow::tickTask);
    connect(processes_, &runtime::ProcessRunner::started, this, [this](const QString& label, const QString& command) {
        bottom_->showBuildOutput();
        log(QStringLiteral("▸ %1").arg(label));
        log(QStringLiteral("  %1").arg(command));
        showTask(label);
        updateActions();
    });
    connect(processes_, &runtime::ProcessRunner::outputLine, this, &MainWindow::log);
    connect(processes_, &runtime::ProcessRunner::finished, this,
            [this](const QString& label, const sdk::ProcessResult& result) {
                const qint64 elapsed = taskClock_.isValid() ? taskClock_.elapsed() : 0;
                hideTask();
                if (result.succeeded()) {
                    log(tr("  %1 finished in %2 s.").arg(label, QLocale().toString(elapsed / 1000.0, 'f', 1)));
                } else if (result.cancelled) {
                    log(tr("  %1 cancelled.").arg(label));
                } else if (!result.error.isEmpty()) {
                    log(QStringLiteral("  %1").arg(result.error));
                } else {
                    log(tr("  %1 failed with exit code %2.").arg(label).arg(result.exitCode));
                }
                updateActions();
            });
    connect(emulator_, &runtime::EmulatorRunner::stateChanged, this, &MainWindow::updateActions);
    // MAME's lines go to Messages, the last of them only.
    connect(emulator_, &runtime::EmulatorRunner::outputLine, this, [this](const QString& line) {
        if (line.trimmed().isEmpty()) return;
        hostMessages_.append(message(sdk::Severity::Info, QStringLiteral("emulator"), line, QString()));
        int count = 0;
        for (int i = hostMessages_.size() - 1; i >= 0; --i) {
            if (hostMessages_.at(i).source == QLatin1String("emulator") && ++count > kEmulatorMessages) {
                hostMessages_.removeAt(i);
            }
        }
        refreshDiagnostics();
    });
    connect(emulator_, &runtime::EmulatorRunner::emulationEvent, this, &MainWindow::showEmulationEvent);
    connect(emulator_, &runtime::EmulatorRunner::emulationChanged, this, &MainWindow::updateActions);

    closeProject();
}

MainWindow::~MainWindow() {
    // The runners outlive this destructor (they are children); a task they
    // stop on the way out must not call back into a window half gone.
    processes_->disconnect(this);
    emulator_->disconnect(this);
    processes_->abandon();
    // Views hold raw pointers to the model, the model to the session.
    releaseEditor();
    session_.reset();
}

void MainWindow::releaseEditor() {
    editor_.reset();
    model_.reset();
    parkedEditor_.reset();
    parkedModel_.reset();
    parkedDocument_.clear();
}

// ---------------------------------------------------------------- building

void MainWindow::buildMenus() {
    QMenuBar* bar = menus();
    const QFont menuFont = theme::font(theme::TextRole::Body, theme::size::kMenu);

    QMenu* file = bar->addMenu(tr("File"));
    file->setFont(menuFont);
    QAction* open = file->addAction(tr("Open project…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::chooseProject);
    QAction* openRom = file->addAction(tr("Open ROM…"));
    openRom->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(openRom, &QAction::triggered, this, &MainWindow::openRom);
    recentMenu_ = file->addMenu(tr("Recent projects"));
    recentMenu_->setFont(menuFont);
    switchRevisionAction_ = file->addAction(tr("Open another version…"));
    connect(switchRevisionAction_, &QAction::triggered, this, [this] {
        const QString descriptor = currentDescriptor();
        if (!descriptor.isEmpty() && confirmDiscard()) openGameRepository(descriptor);
    });
    file->addSeparator();
    patchAction_ = file->addAction(tr("Create IPS patch…"));
    connect(patchAction_, &QAction::triggered, this, &MainWindow::createPatch);
    resetAction_ = file->addAction(tr("Reset version to the original…"));
    connect(resetAction_, &QAction::triggered, this, &MainWindow::resetRevision);
    file->addSeparator();
    saveAction_ = file->addAction(tr("Save all"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveAll);
    closeAction_ = file->addAction(tr("Close project"));
    connect(closeAction_, &QAction::triggered, this, [this] {
        if (confirmDiscard(false)) {
            closeProject();
        }
    });
    file->addSeparator();
    QAction* settings = file->addAction(tr("Settings…"));
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settings, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        bool restart = false;
        connect(&dialog, &SettingsDialog::restartRequested, this, [&restart] { restart = true; });
        dialog.exec();
        if (restart) restartStudio();
    });
    file->addSeparator();
    QAction* quit = file->addAction(tr("Exit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* edit = bar->addMenu(tr("Edit"));
    edit->setFont(menuFont);
    undoAction_ = edit->addAction(tr("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = edit->addAction(tr("Redo"));
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    connect(undoAction_, &QAction::triggered, this, [this] {
        if (tasEditorShown()) {
            tasEditor_->undoStack()->undo();
        } else if (session_) {
            session_->undoStack()->undo();
        }
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (tasEditorShown()) {
            tasEditor_->undoStack()->redo();
        } else if (session_) {
            session_->undoStack()->redo();
        }
    });
    // The TAS editor's frame, one at a time; off while it is not shown, so
    // the keys stay the editors' own.
    edit->addSeparator();
    cutFrameAction_ = edit->addAction(tr("Cut frame"));
    cutFrameAction_->setShortcut(QKeySequence::Cut);
    copyFrameAction_ = edit->addAction(tr("Copy frame"));
    copyFrameAction_->setShortcut(QKeySequence::Copy);
    pasteFrameAction_ = edit->addAction(tr("Paste frame"));
    pasteFrameAction_->setShortcut(QKeySequence::Paste);
    insertFrameAction_ = edit->addAction(tr("Insert frame"));
    insertFrameAction_->setShortcut(QKeySequence(Qt::Key_Insert));
    deleteFrameAction_ = edit->addAction(tr("Delete frame"));
    deleteFrameAction_->setShortcut(QKeySequence::Delete);
    const QList<QPair<QAction*, void (TasEditorWindow::*)()>> frameActions{
        {cutFrameAction_, &TasEditorWindow::cut},
        {copyFrameAction_, &TasEditorWindow::copy},
        {pasteFrameAction_, &TasEditorWindow::paste},
        {insertFrameAction_, &TasEditorWindow::insertFrame},
        {deleteFrameAction_, &TasEditorWindow::deleteFrame}};
    for (const auto& frameAction : frameActions) {
        frameAction.first->setEnabled(false);
        const auto method = frameAction.second;
        connect(frameAction.first, &QAction::triggered, this, [this, method] {
            if (tasEditorShown()) (tasEditor_->*method)();
        });
    }

    QMenu* view = bar->addMenu(tr("View"));
    view->setFont(menuFont);
    QAction* dark = view->addAction(tr("Dark theme"));
    QAction* light = view->addAction(tr("Light theme"));
    dark->setCheckable(true);
    light->setCheckable(true);
    auto* themes = new QActionGroup(view);
    themes->addAction(dark);
    themes->addAction(light);
    (theme::Theme::instance().mode() == theme::Mode::Dark ? dark : light)->setChecked(true);
    connect(dark, &QAction::triggered, this, [] { theme::Theme::instance().setMode(theme::Mode::Dark); });
    connect(light, &QAction::triggered, this, [] { theme::Theme::instance().setMode(theme::Mode::Light); });
    connect(&theme::Theme::instance(), &theme::Theme::changed, view,
            [dark, light](theme::Mode mode) { (mode == theme::Mode::Dark ? dark : light)->setChecked(true); });
    QAction* toggleTheme = view->addAction(tr("Toggle theme"));
    toggleTheme->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(toggleTheme, &QAction::triggered, this, [] { theme::Theme::instance().toggle(); });
    view->addSeparator();
    QAction* panel = view->addAction(tr("Messages panel"));
    panel->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(panel, &QAction::triggered, this, [this] { bottom_->setExpanded(!bottom_->isExpanded()); });

    // The toolbar's buttons, in the same order.
    QMenu* build = bar->addMenu(tr("Run"));
    build->setFont(menuFont);
    buildAction_ = build->addAction(tr("Build"));
    buildAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(buildAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canBuild() && !isBusy()) session_->build();
    });
    playAction_ = build->addAction(tr("Play"));
    playAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
    connect(playAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canPlay() && !isBusy()) {
            showPlaytest();
            session_->play();
        }
    });
    playSelectedAction_ = build->addAction(tr("Play from selected"));
    playSelectedAction_->setShortcut(QKeySequence(QStringLiteral("F4")));
    connect(playSelectedAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canPlay() && !session_->selectedStart().isEmpty() && !isBusy()) {
            // The session reads its start before the Playtest view replaces
            // the editor that sets it.
            session_->playSelected();
            showPlaytest();
        }
    });
    build->addSeparator();
    pauseGameAction_ = build->addAction(tr("Pause"));
    pauseGameAction_->setShortcut(QKeySequence(Qt::Key_Pause));
    connect(pauseGameAction_, &QAction::triggered, this, &MainWindow::togglePlaytestPause);
    stopGameAction_ = build->addAction(tr("Stop"));
    stopGameAction_->setShortcut(QKeySequence(QStringLiteral("Shift+F5")));
    connect(stopGameAction_, &QAction::triggered, this, [this] {
        if (tasEditorShown()) {
            tasEditor_->stop();
        } else {
            emulator_->stop();
        }
    });
    pauseGameAction_->setEnabled(false);
    stopGameAction_->setEnabled(false);

    // Emulation controls, the way FCEUX has them: frame advance, speed in
    // steps, numbered save states. None of them changes a movie being
    // recorded: it keeps the input of every frame.
    frameAdvanceAction_ = build->addAction(tr("Frame advance"));
    frameAdvanceAction_->setShortcut(QKeySequence(Qt::Key_Backslash));
    connect(frameAdvanceAction_, &QAction::triggered, this, [this] {
        // The first press pauses; each one after it runs one frame.
        const bool paused = emulator_->isPaused();
        emulator_->frameAdvance();
        notifyEmulation(paused ? tr("Frame %1").arg(emulator_->movieFrame() + 1) : tr("Paused"));
    });
    speedMenu_ = build->addMenu(tr("Speed"));
    speedMenu_->setFont(menuFont);
    speedGroup_ = new QActionGroup(this);
    for (const int percent : kSpeeds) {
        QAction* action = speedMenu_->addAction(percent == 0 ? tr("No limit") : tr("%1%").arg(percent));
        action->setCheckable(true);
        action->setData(percent);
        action->setChecked(percent == emulator_->speed());
        speedGroup_->addAction(action);
        connect(action, &QAction::triggered, this, [this, percent] { setEmulationSpeed(percent); });
    }
    speedMenu_->addSeparator();
    slowerAction_ = speedMenu_->addAction(tr("Slower"));
    slowerAction_->setShortcut(QKeySequence(Qt::Key_Minus));
    connect(slowerAction_, &QAction::triggered, this, [this] { stepEmulationSpeed(-1); });
    fasterAction_ = speedMenu_->addAction(tr("Faster"));
    fasterAction_->setShortcut(QKeySequence(Qt::Key_Equal));
    connect(fasterAction_, &QAction::triggered, this, [this] { stepEmulationSpeed(1); });
    // Over the picture: the buttons held and the game's RAM cells.
    showInputAction_ = build->addAction(tr("Show input"));
    showInputAction_->setCheckable(true);
    showWatchAction_ = build->addAction(tr("Show RAM watch"));
    showWatchAction_->setCheckable(true);
    {
        QSettings settings;
        showInputAction_->setChecked(settings.value(QStringLiteral("emulation/showInput"), false).toBool());
        showWatchAction_->setChecked(settings.value(QStringLiteral("emulation/showWatch"), false).toBool());
        emulator_->setOverlay(showInputAction_->isChecked(), showWatchAction_->isChecked());
    }
    const auto applyOverlay = [this] {
        QSettings settings;
        settings.setValue(QStringLiteral("emulation/showInput"), showInputAction_->isChecked());
        settings.setValue(QStringLiteral("emulation/showWatch"), showWatchAction_->isChecked());
        emulator_->setOverlay(showInputAction_->isChecked(), showWatchAction_->isChecked());
    };
    connect(showInputAction_, &QAction::toggled, this, applyOverlay);
    connect(showWatchAction_, &QAction::toggled, this, applyOverlay);
    build->addSeparator();
    saveStateAction_ = build->addAction(tr("Save state"));
    saveStateAction_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(saveStateAction_, &QAction::triggered, this, [this] {
        const int slot = emulator_->stateSlot();
        const bool replaced = emulator_->slotHasState(slot);
        emulator_->saveState();
        notifyEmulation(replaced ? tr("State %1 saved (replaced)").arg(slot) : tr("State %1 saved").arg(slot));
    });
    loadStateAction_ = build->addAction(tr("Load state"));
    loadStateAction_->setShortcut(QKeySequence(Qt::Key_F8));
    connect(loadStateAction_, &QAction::triggered, this, [this] {
        const int slot = emulator_->stateSlot();
        if (!emulator_->slotHasState(slot)) {
            notifyEmulation(tr("State %1 is empty").arg(slot));
            return;
        }
        emulator_->loadState();
        notifyEmulation(tr("State %1 loaded").arg(slot));
    });
    previousSlotAction_ = build->addAction(tr("Previous state slot"));
    previousSlotAction_->setShortcut(QKeySequence(Qt::Key_F6));
    connect(previousSlotAction_, &QAction::triggered, this, [this] { stepStateSlot(-1); });
    nextSlotAction_ = build->addAction(tr("Next state slot"));
    nextSlotAction_->setShortcut(QKeySequence(Qt::Key_F7));
    connect(nextSlotAction_, &QAction::triggered, this, [this] { stepStateSlot(1); });
    build->addSeparator();
    QMenu* movie = build->addMenu(tr("Movie"));
    movie->setFont(menuFont);
    recordMovieAction_ = movie->addAction(tr("Record movie…"));
    connect(recordMovieAction_, &QAction::triggered, this,
            [this] { startMovie(int(runtime::MovieRequest::Mode::Record)); });
    resumeMovieAction_ = movie->addAction(tr("Resume movie recording…"));
    connect(resumeMovieAction_, &QAction::triggered, this,
            [this] { startMovie(int(runtime::MovieRequest::Mode::Resume)); });
    playMovieAction_ = movie->addAction(tr("Play movie…"));
    connect(playMovieAction_, &QAction::triggered, this,
            [this] { startMovie(int(runtime::MovieRequest::Mode::Play)); });
    movie->addSeparator();
    stopMovieAction_ = movie->addAction(tr("Stop movie"));
    connect(stopMovieAction_, &QAction::triggered, this, [this] { emulator_->stopMovie(); });

    QMenu* help = bar->addMenu(tr("Help"));
    help->setFont(menuFont);
    const auto openLink = [this, help](const QString& text, const char* url) {
        connect(help->addAction(text), &QAction::triggered, this,
                [url] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(url))); });
    };
    openLink(tr("User guide"), links::kUserGuide);
    openLink(tr("Source code"), links::kSource);
    openLink(tr("Report a problem"), links::kIssues);
    help->addSeparator();
    QAction* about = help->addAction(tr("About Retrolarium Studio"));
    connect(about, &QAction::triggered, this, [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });
    rebuildRecentMenu();
}

QWidget* MainWindow::buildToolbar() {
    auto* bar = surface("chrome", this);
    bar->setFixedHeight(46);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(12);

    projectButton_ = new QPushButton(bar);
    projectButton_->setFixedHeight(32);
    projectButton_->setCursor(Qt::PointingHandCursor);
    projectButton_->setToolTip(tr("Switch project"));
    auto* projectLayout = new QHBoxLayout(projectButton_);
    projectLayout->setContentsMargins(6, 0, 10, 0);
    projectLayout->setSpacing(10);
    platformBadge_ = new ui::Badge(QStringLiteral("—"), ui::Badge::Kind::Platform, projectButton_);
    projectName_ = new ui::Label(tr("No project"), ui::Label::Role::Project, projectButton_);
    revisionBadge_ = new ui::Badge(QString(), ui::Badge::Kind::Tag, projectButton_);
    auto* chevron = new QLabel(projectButton_);
    auto setChevron = [chevron] {
        chevron->setPixmap(theme::iconPixmap(QStringLiteral("chevron-down"), 10, theme::current().faint,
                                             chevron->devicePixelRatioF()));
    };
    setChevron();
    connect(&theme::Theme::instance(), &theme::Theme::changed, chevron, setChevron);
    projectLayout->addWidget(platformBadge_);
    projectLayout->addWidget(projectName_);
    projectLayout->addWidget(revisionBadge_);
    projectLayout->addWidget(chevron);
    for (QWidget* child : projectButton_->findChildren<QWidget*>()) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    connect(projectButton_, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.setFont(theme::font(theme::TextRole::Body, theme::size::kMenu));
        QAction* open = menu.addAction(tr("Open project…"));
        connect(open, &QAction::triggered, this, &MainWindow::chooseProject);
        const QStringList recent = QSettings().value(QString::fromLatin1(kRecentKey)).toStringList();
        if (!recent.isEmpty()) {
            menu.addSeparator();
            for (const QString& path : recent) {
                QAction* item = menu.addAction(recentLabel(path));
                item->setToolTip(path);
                connect(item, &QAction::triggered, this, [this, path] { openProject(path); });
            }
        }
        menu.exec(projectButton_->mapToGlobal(QPoint(0, projectButton_->height() + 2)));
    });
    layout->addWidget(projectButton_);

    dirtyIndicator_ = new QWidget(bar);
    auto* dirtyLayout = new QHBoxLayout(dirtyIndicator_);
    dirtyLayout->setContentsMargins(0, 0, 0, 0);
    dirtyLayout->setSpacing(6);
    dirtyLayout->addWidget(new DirtyMark(dirtyIndicator_));
    dirtyLabel_ = new ui::Label(QString(), ui::Label::Role::Small, dirtyIndicator_);
    dirtyLayout->addWidget(dirtyLabel_);
    layout->addWidget(dirtyIndicator_);
    layout->addStretch(1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(2);
    undoButton_ = toolbarButton(QStringLiteral("undo"), tr("Undo (Ctrl+Z)"), bar);
    redoButton_ = toolbarButton(QStringLiteral("redo"), tr("Redo (Ctrl+Y)"), bar);
    saveButton_ = toolbarButton(QStringLiteral("save"), tr("Save all (Ctrl+S)"), bar);
    revertButton_ = toolbarButton(QStringLiteral("revert"), tr("Revert document to original"), bar);
    connect(undoButton_, &QToolButton::clicked, undoAction_, &QAction::trigger);
    connect(redoButton_, &QToolButton::clicked, redoAction_, &QAction::trigger);
    connect(saveButton_, &QToolButton::clicked, this, &MainWindow::saveAll);
    connect(revertButton_, &QToolButton::clicked, this, &MainWindow::revertDocument);
    actions->addWidget(undoButton_);
    actions->addWidget(redoButton_);
    actions->addWidget(saveButton_);
    actions->addWidget(revertButton_);
    actions->addSpacing(6);
    auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, bar);
    separator->setFixedHeight(18);
    actions->addWidget(separator, 0, Qt::AlignVCenter);
    actions->addSpacing(6);
    buildButton_ = new QPushButton(tr("Build"), bar);
    buildButton_->setProperty("size", QStringLiteral("large"));
    buildButton_->setFont(theme::font(theme::TextRole::Body, 12.5));
    buildButton_->setToolTip(tr("Build the ROM (Ctrl+B)"));
    connect(buildButton_, &QPushButton::clicked, this, [this] {
        if (isBusy()) {
            cancel();
        } else {
            buildAction_->trigger();
        }
    });
    actions->addWidget(buildButton_);
    actions->addSpacing(4);
    playButton_ = new QPushButton(tr("Play"), bar);
    playButton_->setProperty("size", QStringLiteral("large"));
    playButton_->setFont(theme::font(theme::TextRole::Body, 12.5));
    playButton_->setIcon(theme::icon(QStringLiteral("play")));
    playButton_->setIconSize(QSize(11, 11));
    playButton_->setToolTip(tr("Save, build and play from power-on (Ctrl+F5)"));
    connect(playButton_, &QPushButton::clicked, playAction_, &QAction::trigger);
    actions->addWidget(playButton_);
    actions->addSpacing(4);
    playSelectedButton_ = new ui::PrimaryButton(tr("Play from selected"), QStringLiteral("play"), bar);
    connect(playSelectedButton_, &QAbstractButton::clicked, playSelectedAction_, &QAction::trigger);
    actions->addWidget(playSelectedButton_);
    // The running game's controls; live only while the emulator runs.
    actions->addSpacing(2);
    pauseButton_ = toolbarButton(QStringLiteral("pause"), tr("Pause the playtest (Pause)"), bar);
    stopButton_ = toolbarButton(QStringLiteral("stop"), tr("Stop the playtest (Shift+F5)"), bar);
    connect(pauseButton_, &QToolButton::clicked, this, &MainWindow::togglePlaytestPause);
    connect(stopButton_, &QToolButton::clicked, stopGameAction_, &QAction::trigger);
    pauseButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    actions->addWidget(pauseButton_);
    actions->addWidget(stopButton_);
    layout->addLayout(actions);
    return bar;
}

QWidget* MainWindow::buildCenter() {
    centerStack_ = new QStackedWidget(this);
    welcome_ = buildWelcome();
    centerStack_->addWidget(welcome_);

    workspace_ = surface("bg", centerStack_);
    auto* layout = new QVBoxLayout(workspace_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* crumb = new QWidget(workspace_);
    crumb->setFixedHeight(34);
    auto* crumbLayout = new QHBoxLayout(crumb);
    crumbLayout->setContentsMargins(14, 0, 14, 0);
    crumbLayout->setSpacing(10);
    crumbKind_ = new ui::Label(QString(), ui::Label::Role::Body, crumb);
    crumbKind_->setTone(QStringLiteral("muted"));
    auto* slash = new ui::Label(QStringLiteral("/"), ui::Label::Role::Body, crumb);
    slash->setTone(QStringLiteral("faint"));
    crumbDocument_ = new ui::Label(QString(), ui::Label::Role::Body, crumb);
    crumbDocument_->setFont(theme::font(theme::TextRole::BodyMedium, theme::size::kBody));
    crumbEdited_ = new ui::Label(tr("● edited"), ui::Label::Role::Meta, crumb);
    crumbEdited_->setTone(QStringLiteral("accent"));
    crumbEditor_ = new ui::Badge(QString(), ui::Badge::Kind::Code, crumb);
    budgetText_ = new ui::Label(QString(), ui::Label::Role::Mono, crumb);
    budgetText_->setFont(theme::font(theme::TextRole::Mono, 11.0));
    budgetText_->setTone(QStringLiteral("muted"));
    budgetBar_ = new ui::BudgetBar(crumb);
    crumbLayout->addWidget(crumbKind_);
    crumbLayout->addWidget(slash);
    crumbLayout->addWidget(crumbDocument_);
    crumbLayout->addWidget(crumbEdited_);
    crumbLayout->addWidget(crumbEditor_);
    crumbLayout->addStretch(1);
    crumbLayout->addWidget(budgetText_);
    crumbLayout->addWidget(budgetBar_, 0, Qt::AlignVCenter);
    layout->addWidget(crumb);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, workspace_));

    toolbarRow_ = new QWidget(workspace_);
    toolbarRow_->setFixedHeight(38);
    toolbarRowLayout_ = new QHBoxLayout(toolbarRow_);
    toolbarRowLayout_->setContentsMargins(14, 0, 14, 0);
    toolbarRowLayout_->setSpacing(0);
    layout->addWidget(toolbarRow_);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, workspace_));

    editorStack_ = new QStackedWidget(workspace_);
    emptyEditor_ = surface("canvas", editorStack_);
    auto* emptyLayout = new QVBoxLayout(emptyEditor_);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(8);
    emptyTitle_ = new ui::Label(QString(), ui::Label::Role::Title, emptyEditor_);
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyNote_ = new ui::Label(QString(), ui::Label::Role::Small, emptyEditor_);
    emptyNote_->setAlignment(Qt::AlignCenter);
    emptyNote_->setWordWrap(true);
    emptyNote_->setMaximumWidth(460);
    emptyLayout->addWidget(emptyTitle_, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(emptyNote_, 0, Qt::AlignHCenter);
    editorStack_->addWidget(emptyEditor_);
    toast_ = new ui::Toast(editorStack_);
    layout->addWidget(editorStack_, 1);

    centerStack_->addWidget(workspace_);
    return centerStack_;
}

QWidget* MainWindow::buildWelcome() {
    auto* page = surface("bg", this);
    auto* outer = new QVBoxLayout(page);
    outer->setAlignment(Qt::AlignCenter);
    auto* column = new QWidget(page);
    column->setFixedWidth(420);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(new ui::BrandMark(64, column));
    layout->addSpacing(14);
    layout->addWidget(new ui::Label(tr("Retrolarium Studio"), ui::Label::Role::Caps, column));
    auto* title = new ui::Label(tr("Open a game to start"), ui::Label::Role::Title, column);
    layout->addWidget(title);
    auto* note = new ui::Label(
        tr("Open your ROM: the studio finds its game, downloads the game's files the first time and opens "
           "that version. A disassembly on disk opens from the .rlj file at the top of its folder."),
        ui::Label::Role::Small, column);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* drop = new ui::Label(tr("Or drop a ROM or an .rlj file on this window."), ui::Label::Role::Small, column);
    drop->setTone(QStringLiteral("faint"));
    drop->setWordWrap(true);
    layout->addWidget(drop);
    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    // Opening a ROM is the way in for most people; a disassembly on disk
    // opens as a project.
    auto* rom = new ui::PrimaryButton(tr("Open ROM…"), QStringLiteral("folder"), column);
    connect(rom, &QAbstractButton::clicked, this, &MainWindow::openRom);
    buttons->addWidget(rom);
    welcomeButtons_ << rom;
    auto* open = new QPushButton(tr("Open project…"), column);
    open->setProperty("size", QStringLiteral("large"));
    open->setFont(theme::font(theme::TextRole::Body, 12.5));
    connect(open, &QPushButton::clicked, this, &MainWindow::chooseProject);
    buttons->addWidget(open);
    welcomeButtons_ << open;
    for (const auto& plugin : sdk::PluginRegistry::instance().plugins()) {
        for (const sdk::ProjectAction& action : plugin->projectActions()) {
            auto* button = new QPushButton(action.text, column);
            button->setProperty("size", QStringLiteral("large"));
            button->setFont(theme::font(theme::TextRole::Body, 12.5));
            const auto run = action.run;
            connect(button, &QPushButton::clicked, this, [this, run] { run(*this); });
            buttons->addWidget(button);
            welcomeButtons_ << button;
        }
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
    // Opening a game runs the kit for a while (reading the versions,
    // downloading, splitting the assets): this says so, and for how long.
    welcomeTask_ = new QWidget(column);
    auto* task = new QVBoxLayout(welcomeTask_);
    task->setContentsMargins(0, 10, 0, 0);
    task->setSpacing(6);
    welcomeTaskLabel_ = new ui::Label(QString(), ui::Label::Role::Small, welcomeTask_);
    welcomeTaskLabel_->setWordWrap(true);
    task->addWidget(welcomeTaskLabel_);
    auto* welcomeBar = new QProgressBar(welcomeTask_);
    welcomeBar->setRange(0, 0);
    welcomeBar->setTextVisible(false);
    task->addWidget(welcomeBar);
    welcomeTask_->hide();
    layout->addWidget(welcomeTask_);
    outer->addWidget(column);
    return page;
}

QWidget* MainWindow::buildStatusBar() {
    auto* bar = surface("chrome", this);
    bar->setFixedHeight(24);
    auto* outer = new QVBoxLayout(bar);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, bar));
    auto* row = new QHBoxLayout;
    row->setContentsMargins(12, 0, 12, 0);
    row->setSpacing(16);
    taskStatus_ = new QWidget(bar);
    auto* task = new QHBoxLayout(taskStatus_);
    task->setContentsMargins(0, 0, 0, 0);
    task->setSpacing(8);
    auto* taskBar = new QProgressBar(taskStatus_);
    taskBar->setRange(0, 0);
    taskBar->setTextVisible(false);
    taskBar->setFixedWidth(96);
    task->addWidget(taskBar, 0, Qt::AlignVCenter);
    taskStatusLabel_ = new ui::Label(QString(), ui::Label::Role::Small, taskStatus_);
    taskStatusLabel_->setFont(theme::font(theme::TextRole::Body, theme::size::kStatus));
    task->addWidget(taskStatusLabel_);
    taskStatus_->hide();
    row->addWidget(taskStatus_);
    statusLeft_ = new ui::Label(QString(), ui::Label::Role::Small, bar);
    statusLeft_->setFont(theme::font(theme::TextRole::Body, theme::size::kStatus));
    row->addWidget(statusLeft_, 1);
    statusSummary_ = new QPushButton(bar);
    statusSummary_->setProperty("variant", QStringLiteral("flat"));
    statusSummary_->setFont(theme::font(theme::TextRole::Body, theme::size::kStatus));
    statusSummary_->setCursor(Qt::PointingHandCursor);
    statusSummary_->setStyleSheet(QStringLiteral("padding: 0 4px; min-height: 18px;"));
    connect(statusSummary_, &QPushButton::clicked, this, [this] { bottom_->showMessages(); });
    row->addWidget(statusSummary_);
    statusRight_ = new ui::Label(QString(), ui::Label::Role::Mono, bar);
    statusRight_->setFont(theme::font(theme::TextRole::Mono, 11.0));
    statusRight_->setTone(QStringLiteral("muted"));
    row->addWidget(statusRight_);
    outer->addLayout(row, 1);
    return bar;
}

void MainWindow::showTask(const QString& label) {
    taskLabel_ = label;
    taskClock_.start();
    tickTask();
    taskStatus_->show();
    welcomeTask_->show();
    for (QWidget* button : welcomeButtons_) {
        button->setEnabled(false);
    }
    taskTimer_->start();
}

void MainWindow::tickTask() {
    const QString text = tr("%1… %2 s").arg(taskLabel_).arg(taskClock_.elapsed() / 1000);
    taskStatusLabel_->setText(text);
    welcomeTaskLabel_->setText(text);
}

void MainWindow::hideTask() {
    taskTimer_->stop();
    taskClock_.invalidate();
    taskStatus_->hide();
    welcomeTask_->hide();
    for (QWidget* button : welcomeButtons_) {
        button->setEnabled(true);
    }
}

// ---------------------------------------------------------------- projects

QWidget* MainWindow::dialogParent() const { return const_cast<MainWindow*>(this); }

void MainWindow::openProject(const QString& path) {
    if (confirmDiscard()) {
        openProjectFile(path);
    }
}

void MainWindow::report(const sdk::Diagnostic& diagnostic) {
    hostMessages_.append(diagnostic);
    refreshDiagnostics();
    if (diagnostic.severity != sdk::Severity::Info) {
        bottom_->showMessages();
    }
}

void MainWindow::refuseAction(const QString& message) {
    updateStatus(message);
    if (message.isEmpty()) return;
    if (toast_) toast_->showMessage(message);
    // Also in Messages, so the reason stays readable after the toast: once
    // while the same refusal repeats.
    const QString source = document_.isEmpty() ? kind_ : kind_ + QLatin1Char('/') + document_;
    if (!hostMessages_.isEmpty() && hostMessages_.last().message == message && hostMessages_.last().source == source) {
        return;
    }
    sdk::Diagnostic d;
    d.severity = sdk::Severity::Warning;
    d.source = source;
    d.message = message;
    d.location = QFileInfo(projectPath_).fileName();
    d.kind = kind_;
    d.document = document_;
    report(d);
}

void MainWindow::log(const QString& line) { bottom_->appendLog(line); }

void MainWindow::chooseProject() {
    if (!confirmDiscard()) {
        return;
    }
    const QString start = projectPath_.isEmpty() ? QString() : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), start,
                                                      tr("Retrolarium projects (*.rlj);;All files (*)"));
    if (!path.isEmpty()) {
        openProjectFile(path);
    }
}

bool MainWindow::openProjectFile(const QString& path, bool prepared) {
    QString error;
    const sdk::ProjectFile project = sdk::ProjectFile::load(path, &error);
    if (!project.isValid()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, QFileInfo(path).fileName()));
        return false;
    }
    if (project.string(QStringLiteral("schema")) == QLatin1String("retrolarium.game")) {
        openGameRepository(project.path());
        return true;
    }
    // A version of a game repository (Recent, the command line) is got ready
    // by the kit first, as when the game is opened: a newer kit brings the
    // project up to date with it, the game's sources included.
    const QString repository = prepared ? QString() : repositoryDescriptor(project.path());
    if (!repository.isEmpty()) {
        prepareRevision(repository, QFileInfo(project.path()).absoluteDir().dirName(), QString(), project.path());
        return true;
    }
    sdk::Plugin* plugin = sdk::PluginRegistry::instance().pluginFor(project);
    if (!plugin) {
        report(message(sdk::Severity::Error, QStringLiteral("project"),
                       tr("No plugin opens profile '%1' (%2).")
                           .arg(project.string(QStringLiteral("project.profile")),
                                project.string(QStringLiteral("project.platform"))),
                       QFileInfo(path).fileName()));
        return false;
    }
    // The game's own names (config/names.json in its repository) are in place
    // before the session builds its catalogs; the open project keeps its own
    // if this one does not open.
    const sdk::GameNames previousNames = sdk::GameNames::current();
    QString namesError;
    sdk::GameNames::install(sdk::GameNames::forProject(project, &namesError));
    if (!namesError.isEmpty()) {
        report(message(sdk::Severity::Warning, plugin->id(), namesError, QFileInfo(path).fileName()));
    }
    std::unique_ptr<sdk::Session> session = plugin->open(project, &error);
    if (!session) {
        sdk::GameNames::install(previousNames);
        report(message(sdk::Severity::Error, plugin->id(), error, QFileInfo(path).fileName()));
        return false;
    }
    // A kit task may be writing the game's folder: opening another project
    // stops it, the user's call as on exit.
    if (isBusy() &&
        !frame::MessageDialog::confirm(this, tr("Open project"), tr("A task is still running"),
                                       tr("Opening another project stops it before it finishes."), tr("Open"))) {
        sdk::GameNames::install(previousNames);
        return false;
    }
    // Open ROM and version changes reach here after a download or a dialog,
    // with the current project still editable all the while.
    if (!confirmDiscard()) {
        sdk::GameNames::install(previousNames);
        return false;
    }

    closeProject();
    session_ = std::move(session);
    session_->attachHost(this);
    projectPath_ = project.path();
    rememberRecent(projectPath_);
    const QString descriptor = currentDescriptor();
    emulator_->setWatch(descriptor.isEmpty() ? QVector<runtime::WatchCell>()
                                             : watchCells(QFileInfo(descriptor).absolutePath()));

    QUndoStack* undo = session_->undoStack();
    connect(undo, &QUndoStack::indexChanged, this, [this] {
        scheduleDirtyState();
        updateStatus();
    });
    connect(undo, &QUndoStack::cleanChanged, this, [this] { scheduleDirtyState(); });
    connect(session_.get(), &sdk::Session::documentsChanged, this, [this](const QString& kind) {
        if (kind == kind_) {
            documents_->updateGroups(session_->documents(kind_));
        }
        scheduleDirtyState();
    });
    connect(session_.get(), &sdk::Session::diagnosticsChanged, this, &MainWindow::refreshDiagnostics);
    // "Don't save" was said of the edits as they were: any edit since asks again.
    connect(session_->undoStack(), &QUndoStack::indexChanged, this, [this] { discardedSession_ = nullptr; });
    connect(session_.get(), &sdk::Session::selectedStartChanged, this, &MainWindow::updateActions);
    connect(
        session_.get(), &sdk::Session::documentRequested, this,
        [this](const QString& kind, const QString& document) {
            if (!session_ || kind.isEmpty()) return;
            selectKind(kind);
            if (!document.isEmpty()) {
                documents_->selectDocument(document);
                selectDocument(document);
            }
        },
        Qt::QueuedConnection);

    updateProjectChrome();
    const QVector<sdk::Kind> kinds = session_->kinds();
    rail_->setKinds(kinds);
    centerStack_->setCurrentWidget(workspace_);
    refreshDiagnostics();
    if (!kinds.isEmpty()) {
        selectKind(kinds.first().id);
    }
    updateDirtyState();
    return true;
}

namespace {

QString processError(const sdk::ProcessResult& result) {
    // A run that started and still carries an error was stopped by the
    // runner (a timeout); its stderr is only what it printed so far.
    if (result.started && !result.error.isEmpty()) return result.error;
    const QString text = QString::fromUtf8(result.standardError).trimmed();
    return text.isEmpty() ? result.error : text.section(QLatin1Char('\n'), -3);
}

}  // namespace

void MainWindow::openGameRepository(const QString& descriptor) {
    const QString name = QFileInfo(descriptor).fileName();
    if (isBusy()) {
        report(message(sdk::Severity::Warning, QStringLiteral("project"),
                       tr("Wait for the running task to finish, then open it again."), name));
        return;
    }
    QString error;
    sdk::ProcessSpec spec = kitCommand({QStringLiteral("studio"), QStringLiteral("revisions"), descriptor},
                                       QFileInfo(descriptor).absolutePath(), &error);
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, name));
        return;
    }
    spec.label = tr("Read the versions of %1").arg(name);
    runProcess(spec, [this, descriptor, name](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(message(sdk::Severity::Error, QStringLiteral("project"),
                               tr("Reading the game's versions failed: %1").arg(processError(result)), name));
            }
            return;
        }
        const QJsonObject r = QJsonDocument::fromJson(result.standardOutput).object();
        QVector<RevisionChoice> choices;
        for (const QJsonValue& v : r.value(QStringLiteral("revisions")).toArray()) {
            const QJsonObject o = v.toObject();
            choices << RevisionChoice{
                o.value(QStringLiteral("id")).toString(),
                o.value(QStringLiteral("label")).toString(),
                o.value(QStringLiteral("rom")).toString(),
                o.value(QStringLiteral("rom_status")).toString(),
                o.value(QStringLiteral("edited")).toBool(),
                o.value(QStringLiteral("built_from")).toObject().value(QStringLiteral("label")).toString()};
        }
        const QJsonObject split = r.value(QStringLiteral("split")).toObject();
        const QString splitStatus = split.value(QStringLiteral("status")).toString();
        QString note;
        if (splitStatus == QLatin1String("needed")) {
            note = tr("The game's files are not split from the ROM yet; that runs when you open a version.");
        } else if (splitStatus == QLatin1String("unavailable")) {
            note = tr("Building from the game's sources needs the %1 ROM (%2); editing does not.")
                       .arg(split.value(QStringLiteral("needs")).toString(),
                            split.value(QStringLiteral("rom")).toString());
        }
        rememberRepository(descriptor);
        const QString key = QStringLiteral("gameRevisions/") + QFileInfo(descriptor).absoluteFilePath();
        RevisionDialog dialog(r.value(QStringLiteral("game")).toObject().value(QStringLiteral("name")).toString(),
                              choices, QSettings().value(key).toString(), note, this);
        if (dialog.exec() != QDialog::Accepted || dialog.revision().isEmpty()) return;
        QSettings().setValue(key, dialog.revision());
        prepareRevision(descriptor, dialog.revision(), QString());
    });
}

void MainWindow::rememberRepository(const QString& descriptor) {
    QSettings settings;
    QStringList known = settings.value(QString::fromLatin1(kRepositoriesKey)).toStringList();
    const QString path = QFileInfo(descriptor).absoluteFilePath();
    if (!known.contains(path)) {
        known << path;
        settings.setValue(QString::fromLatin1(kRepositoriesKey), known);
    }
}

void MainWindow::openRom() {
    if (isBusy() || !confirmDiscard()) return;
    const QString rom =
        QFileDialog::getOpenFileName(this, tr("Open ROM"), QDir::homePath(),
                                     tr("ROM images (*.nes *.fds *.bin *.md *.gen *.sfc *.smc);;All files (*)"));
    if (rom.isEmpty()) return;
    openRomFile(rom);
}

// A file dropped on the window: a project opens, any other file is taken
// for a ROM, as Open ROM does.
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile() && QFileInfo(urls.first().toLocalFile()).isFile() && !isBusy()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() != 1 || !urls.first().isLocalFile()) return;
    const QString path = urls.first().toLocalFile();
    event->acceptProposedAction();
    // Opened once the drop is over: a dialog may come up.
    QTimer::singleShot(0, this, [this, path] {
        if (isBusy()) return;
        if (path.endsWith(QLatin1String(".rlj"), Qt::CaseInsensitive)) {
            openProject(path);
        } else if (confirmDiscard()) {
            openRomFile(path);
        }
    });
}

namespace {

QString fileSha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

// "nes" for an iNES image, "megadrive" for a cartridge with SEGA at $100,
// empty for anything else (then only its bytes and header are shown).
QString romPlatform(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = file.read(0x200);
    if (head.startsWith(QByteArray("NES\x1a", 4))) return QStringLiteral("nes");
    if (head.size() >= 0x110 && head.mid(0x100, 16).contains("SEGA")) return QStringLiteral("megadrive");
    return {};
}

QString tomlString(const QString& value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// A known game's ROM: set up its source, or open it as it is? Asked unless
// the answer was kept.
bool askSetUpSource(QWidget* parent, const QString& name, bool* remember) {
    frame::StudioDialog dialog(QObject::tr("Open ROM"), parent);
    auto* layout = new QVBoxLayout(dialog.body());
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    auto* heading = new QLabel(name, &dialog);
    heading->setFont(theme::font(theme::TextRole::Heading, 18.0));
    auto* text = new QLabel(QObject::tr("The studio knows this game. It can set up its source (download the game's "
                                        "repository) and open it with the game's own editors, or open the ROM as it "
                                        "is: its bytes, its tiles, its header and a playtest."),
                            &dialog);
    text->setWordWrap(true);
    text->setFont(theme::font(theme::TextRole::Body, 13.0));
    auto* keep = new QCheckBox(QObject::tr("Do not ask again"), &dialog);
    keep->setFont(theme::font(theme::TextRole::Body, 13.0));
    auto* buttons = new QHBoxLayout;
    auto* asRom = new QPushButton(QObject::tr("Open the ROM"), &dialog);
    asRom->setAutoDefault(false);
    auto* source = new QPushButton(QObject::tr("Set up the source"), &dialog);
    source->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(asRom);
    buttons->addWidget(source);
    layout->addWidget(heading);
    layout->addWidget(text);
    layout->addWidget(keep);
    layout->addLayout(buttons);
    dialog.setMinimumWidth(460);
    QObject::connect(source, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(asRom, &QPushButton::clicked, &dialog, &QDialog::reject);
    const bool setUp = dialog.exec() == QDialog::Accepted;
    *remember = keep->isChecked();
    return setUp;
}

}  // namespace

void MainWindow::openRomFile(const QString& rom) {
    // A ROM opened as it is before, or saved from such a project, opens that
    // project again.
    const QString digest = fileSha256(rom);
    const QString remembered = QSettings().value(QStringLiteral("romProjects/") + digest).toString();
    if (!remembered.isEmpty() && QFileInfo::exists(remembered)) {
        openProjectFile(remembered);
        return;
    }
    QString error;
    sdk::ProcessSpec spec =
        kitCommand({QStringLiteral("studio"), QStringLiteral("identify"), rom}, QFileInfo(rom).absolutePath(), &error);
    if (spec.program.isEmpty()) {
        openRomAsProject(rom);
        return;
    }
    spec.label = tr("Identify %1").arg(QFileInfo(rom).fileName());
    runProcess(spec, [this, rom](const sdk::ProcessResult& result) {
        const QJsonArray matches =
            QJsonDocument::fromJson(result.standardOutput).object().value(QStringLiteral("matches")).toArray();
        if (!result.succeeded() || matches.isEmpty()) {
            openRomAsProject(rom);
            return;
        }
        const QString choice = QSettings().value(QStringLiteral("openRom/choice")).toString();
        bool setUp = choice == QLatin1String("source");
        if (choice.isEmpty()) {
            bool remember = false;
            setUp =
                askSetUpSource(this, matches.first().toObject().value(QStringLiteral("name")).toString(), &remember);
            if (remember) {
                QSettings().setValue(QStringLiteral("openRom/choice"),
                                     setUp ? QStringLiteral("source") : QStringLiteral("rom"));
            }
        }
        if (setUp) {
            setUpGameSource(rom);
        } else {
            openRomAsProject(rom);
        }
    });
}

void MainWindow::openRomAsProject(const QString& rom) {
    // Documents/Retrolarium/ROMs/<name>: the ROM to edit, the original
    // beside it, and the project; the same ROM finds its folder again.
    const QFileInfo info(rom);
    const QString digest = fileSha256(rom);
    const QDir root(QDir(documentsDirectory()).filePath(QStringLiteral("Retrolarium/ROMs")));
    QString stem = info.completeBaseName();
    stem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    QString folder = root.filePath(stem);
    for (int n = 2; QFileInfo::exists(folder); ++n) {
        const QString project = QDir(folder).filePath(stem + QStringLiteral(".rlj"));
        QString error;
        if (sdk::ProjectFile::load(project, &error).string(QStringLiteral("source.sha256")) == digest) {
            QSettings().setValue(QStringLiteral("romProjects/") + digest, project);
            openProjectFile(project);
            return;
        }
        folder = root.filePath(QStringLiteral("%1-%2").arg(stem).arg(n));
    }
    const QString file = info.fileName();
    const QDir dir(folder);
    if (!QDir().mkpath(dir.filePath(QStringLiteral("original"))) ||
        !QFile::copy(rom, dir.filePath(QStringLiteral("original/") + file)) || !QFile::copy(rom, dir.filePath(file))) {
        // The folder is new: gone again, it does not push the next try to another name.
        QDir(folder).removeRecursively();
        report(message(sdk::Severity::Error, QStringLiteral("project"),
                       tr("The ROM could not be copied into %1.").arg(QDir::toNativeSeparators(folder)), file));
        return;
    }
    QFile::setPermissions(dir.filePath(file), QFile::permissions(dir.filePath(file)) | QFileDevice::WriteOwner);
    const QString platform = romPlatform(rom);
    const QString text = QStringLiteral(
                             "# A ROM opened as it is in Retrolarium Studio.\n"
                             "schema = \"retrolarium.project\"\nschema_version = 1\n\n"
                             "[project]\nid = %1\nname = %2\nplatform = %3\nprofile = \"rom.v1\"\n"
                             "source_mode = \"rom\"\n\n[source]\nrom = %4\noriginal = %5\nsha256 = \"%6\"\n")
                             .arg(tomlString(stem), tomlString(info.completeBaseName()),
                                  tomlString(platform.isEmpty() ? QStringLiteral("unknown") : platform),
                                  tomlString(file), tomlString(QStringLiteral("original/") + file), digest);
    const QString project = dir.filePath(stem + QStringLiteral(".rlj"));
    QFile out(project);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(text.toUtf8()) < 0 || !out.flush()) {
        const QString problem = out.errorString();
        out.close();
        QDir(folder).removeRecursively();
        report(message(sdk::Severity::Error, QStringLiteral("project"), problem, file));
        return;
    }
    out.close();
    QSettings().setValue(QStringLiteral("romProjects/") + digest, project);
    report(message(sdk::Severity::Info, QStringLiteral("project"),
                   tr("Opened %1 as it is, in %2.").arg(file, QDir::toNativeSeparators(folder)), file));
    openProjectFile(project);
}

void MainWindow::setUpGameSource(const QString& rom) {
    // Downloaded games go where the user finds them: their edits live there.
    const QString games = QDir(documentsDirectory()).filePath(QStringLiteral("Retrolarium"));
    QStringList arguments{QStringLiteral("studio"), QStringLiteral("open-rom"), rom, QStringLiteral("--games"), games};
    for (const QString& known : QSettings().value(QString::fromLatin1(kRepositoriesKey)).toStringList()) {
        if (QFileInfo(known).isFile()) arguments << QStringLiteral("--known") << known;
    }
    QString error;
    sdk::ProcessSpec spec = kitCommand(arguments, QFileInfo(rom).absolutePath(), &error);
    const QString name = QFileInfo(rom).fileName();
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, name));
        return;
    }
    spec.label = tr("Open %1").arg(name);
    runProcess(spec, [this, name, rom](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                frame::MessageDialog::inform(this, tr("Open ROM"), tr("The game could not be opened"),
                                             tr("Getting its files failed: %1").arg(processError(result)));
            }
            return;
        }
        const QJsonObject r = QJsonDocument::fromJson(result.standardOutput).object();
        const QString status = r.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("ready")) {
            const QString descriptor = r.value(QStringLiteral("descriptor")).toString();
            rememberRepository(descriptor);
            QSettings().setValue(QStringLiteral("gameRevisions/") + QFileInfo(descriptor).absoluteFilePath(),
                                 r.value(QStringLiteral("revision")).toString());
            if (r.value(QStringLiteral("downloaded")).toBool()) {
                report(message(sdk::Severity::Info, QStringLiteral("project"),
                               tr("Downloaded the game's files into %1.")
                                   .arg(QDir::toNativeSeparators(QFileInfo(descriptor).absolutePath())),
                               name));
            }
            openProjectFile(r.value(QStringLiteral("project")).toString(), true);
        } else {
            // A game the studio has no editors for, or a ROM it does not
            // know after all: it opens as it is.
            openRomAsProject(rom);
        }
    });
}

void MainWindow::prepareRevision(const QString& descriptor, const QString& revision, const QString& rom,
                                 const QString& project) {
    const QString name = QFileInfo(descriptor).fileName();
    QStringList arguments{QStringLiteral("studio"), QStringLiteral("prepare"), descriptor, QStringLiteral("--revision"),
                          revision};
    if (!rom.isEmpty()) arguments << QStringLiteral("--rom") << rom;
    QString error;
    sdk::ProcessSpec spec = kitCommand(arguments, QFileInfo(descriptor).absolutePath(), &error);
    // A project opened by its own file still opens when the kit cannot get
    // it ready; the reason is in Messages.
    const auto openAsItIs = [this, project] {
        if (!project.isEmpty()) openProjectFile(project, true);
    };
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, name));
        openAsItIs();
        return;
    }
    spec.label = tr("Get %1 ready").arg(revision);
    runProcess(spec, [this, descriptor, revision, name, project, openAsItIs](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(message(sdk::Severity::Error, QStringLiteral("project"),
                               tr("Getting the game ready failed: %1").arg(processError(result)), name));
            }
            openAsItIs();
            return;
        }
        const QJsonObject r = QJsonDocument::fromJson(result.standardOutput).object();
        const QString status = r.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("ready")) {
            const QJsonObject split = r.value(QStringLiteral("split")).toObject();
            if (split.value(QStringLiteral("status")).toString() == QLatin1String("failed")) {
                report(
                    message(sdk::Severity::Warning, QStringLiteral("project"),
                            tr("The asset split failed; editing works from the ROM, building from "
                               "the sources needs it. %1")
                                .arg(split.value(QStringLiteral("output")).toString().section(QLatin1Char('\n'), -3)),
                            name));
            }
            openProjectFile(r.value(QStringLiteral("project")).toString(), true);
            return;
        }
        if (status == QLatin1String("build-failed")) {
            // A version the repository builds (an expanded layout) did not build.
            report(message(sdk::Severity::Error, QStringLiteral("project"),
                           tr("Building this version from the game's sources failed: %1")
                               .arg(r.value(QStringLiteral("message")).toString().section(QLatin1Char('\n'), -3)),
                           name));
            openAsItIs();
            return;
        }
        if (!project.isEmpty()) {
            // A project opened by its own file: its ROM was there when it was
            // made, so a missing one is reported, not asked for.
            report(message(sdk::Severity::Warning, QStringLiteral("project"),
                           r.value(QStringLiteral("message")).toString(), name));
            openAsItIs();
            return;
        }
        // The revision's ROM is missing, or the file given is another one.
        frame::MessageDialog ask(tr("Choose the ROM"),
                                 status == QLatin1String("needs-rom") ? tr("This version needs its ROM")
                                                                      : tr("That is not this version's ROM"),
                                 r.value(QStringLiteral("message")).toString() +
                                     tr(" The studio copies it next to the game's files and never changes it."),
                                 this);
        ask.addButton(tr("Cancel"), 0);
        ask.addButton(tr("Choose ROM…"), 1, true);
        if (ask.exec() != 1) return;
        const QString file =
            QFileDialog::getOpenFileName(this, tr("Choose the ROM"), QDir::homePath(),
                                         tr("ROM images (*.nes *.fds *.bin *.md *.gen *.sfc *.smc);;All files (*)"));
        if (!file.isEmpty()) prepareRevision(descriptor, revision, file);
    });
}

QString MainWindow::currentDescriptor() const {
    if (!session_ || projectPath_.isEmpty()) return {};
    return repositoryDescriptor(projectPath_);
}

void MainWindow::resetRevision() {
    const QString descriptor = currentDescriptor();
    if (descriptor.isEmpty() || isBusy()) return;
    const QString revision = QFileInfo(projectPath_).absoluteDir().dirName();
    const sdk::Profile profile = session_->profile();
    if (!frame::MessageDialog::confirm(
            this, tr("Reset to the original"), tr("Discard every edit to %1 (%2)?").arg(profile.name, profile.revision),
            tr("The game's data goes back to how it is in the ROM. Saved and unsaved edits to this "
               "version are lost; other versions keep theirs."),
            tr("Discard edits"))) {
        return;
    }
    QString error;
    sdk::ProcessSpec spec = kitCommand(
        {QStringLiteral("studio"), QStringLiteral("reset"), descriptor, QStringLiteral("--revision"), revision},
        QFileInfo(descriptor).absolutePath(), &error);
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, QFileInfo(descriptor).fileName()));
        return;
    }
    const QString project = projectPath_;
    // The session's unsaved edits go with the saved ones.
    closeProject();
    spec.label = tr("Reset %1").arg(revision);
    runProcess(spec, [this, project, descriptor](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(message(sdk::Severity::Error, QStringLiteral("project"),
                               tr("Resetting failed: %1").arg(processError(result)), QFileInfo(descriptor).fileName()));
            }
        }
        openProjectFile(project);
    });
}

void MainWindow::createPatch() {
    if (!session_ || isBusy()) return;
    QString error;
    if (session_->isModified() && !session_->save(&error)) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, QFileInfo(projectPath_).fileName()));
        return;
    }
    QString error2;
    const sdk::ProjectFile project = sdk::ProjectFile::load(projectPath_, &error2);
    const QString suggested =
        QDir(QDir::homePath()).filePath(project.string(QStringLiteral("project.id")) + QStringLiteral(".ips"));
    const QString output =
        QFileDialog::getSaveFileName(this, tr("Create IPS patch"), suggested, tr("IPS patches (*.ips)"));
    if (output.isEmpty()) return;
    sdk::ProcessSpec spec = kitCommand(
        {QStringLiteral("studio"), QStringLiteral("patch"), projectPath_, QStringLiteral("--output"), output},
        QFileInfo(projectPath_).absolutePath(), &error);
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, QFileInfo(projectPath_).fileName()));
        return;
    }
    spec.label = tr("Create %1").arg(QFileInfo(output).fileName());
    // Flicky builds its levels with the studio's native writer.
    const QString writer = toolPath(QStringLiteral("retrolarium-flicky-level-writer"));
    if (!writer.isEmpty()) spec.environment.insert(QStringLiteral("RETROLARIUM_FLICKY_LEVEL_WRITER"), writer);
    runProcess(spec, [this, output](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(message(sdk::Severity::Error, QStringLiteral("project"),
                               tr("Creating the patch failed: %1").arg(processError(result)),
                               QFileInfo(output).fileName()));
            }
            return;
        }
        const QJsonObject r = QJsonDocument::fromJson(result.standardOutput).object();
        frame::MessageDialog::inform(this, tr("Create IPS patch"), tr("Patch created"),
                                     tr("%1 changes %2 bytes of the original ROM. Apply it to that ROM "
                                        "with any IPS patcher.")
                                         .arg(QFileInfo(output).fileName())
                                         .arg(r.value(QStringLiteral("changed_bytes")).toInt()));
    });
}

void MainWindow::closeProject() {
    inspector_->showMessage(QString(), QString(), QString());
    // A running task's callback belongs to the session about to go.
    processes_->abandon();
    emulator_->stopNow();
    emulator_->clearLog();
    audio_->stop();
    releaseEditor();
    session_.reset();
    discardedSession_ = nullptr;
    projectPath_.clear();
    kind_.clear();
    document_.clear();
    lastDocument_.clear();
    rail_->setKinds({});
    documents_->setTitle(QString());
    documents_->setGroups({});
    centerStack_->setCurrentWidget(welcome_);
    hostMessages_.clear();
    refreshDiagnostics();
    // The build output and the emulator's lines were the closed project's.
    bottom_->clearLog();
    updateProjectChrome();
    updateToolbarRow();
    updateDirtyState();
    updateStatus();
}

bool MainWindow::confirmDiscard(bool remembered) {
    if (!session_ || !session_->isModified()) {
        return true;
    }
    // An open asks before its dialog and again as the project changes; the
    // answer holds for the second time. Closing always asks: an open given
    // up (the dialog cancelled) leaves the edits there.
    const int index = session_->undoStack()->index();
    if (remembered && discardedSession_ == session_.get() && discardedIndex_ == index) {
        return true;
    }
    enum Answer { Cancel, Discard, Save };
    frame::MessageDialog box(tr("Unsaved changes"), tr("Save changes to %1?").arg(session_->profile().name),
                             tr("The project has edits that are not saved. Closing without "
                                "saving loses them."),
                             this);
    box.addButton(tr("Don't save"), Discard);
    box.addButton(tr("Cancel"), Cancel);
    box.addButton(tr("Save"), Save, true);
    box.setCancelResult(Cancel);
    const int answer = box.exec();
    if (answer == Cancel) {
        return false;
    }
    if (answer == Save) {
        QString error;
        if (!session_->save(&error)) {
            report(message(sdk::Severity::Error, QStringLiteral("save"), error, QString()));
            return false;
        }
    } else {
        discardedSession_ = session_.get();
        discardedIndex_ = index;
    }
    return true;
}

bool MainWindow::confirmMovieEdits() {
    if (!tasEditor_ || !tasEditor_->isModified()) return true;
    enum Answer { Cancel, Discard, Save };
    frame::MessageDialog box(tr("Unsaved movie"),
                             tr("Save the changes to %1?").arg(QFileInfo(tasEditor_->origin()).fileName()),
                             tr("The TAS editor has edits that are not saved. Going on without saving "
                                "loses them."),
                             this);
    box.addButton(tr("Don't save"), Discard);
    box.addButton(tr("Cancel"), Cancel);
    box.addButton(tr("Save"), Save, true);
    box.setCancelResult(Cancel);
    const int answer = box.exec();
    if (answer == Cancel) return false;
    if (answer == Save) return tasEditor_->save();
    tasEditor_->discard();
    return true;
}

void MainWindow::restartStudio() {
    // The same project at the same document, in the same theme.
    QStringList arguments{QStringLiteral("--theme"), theme::modeName(theme::Theme::instance().mode())};
    if (!projectPath_.isEmpty()) {
        arguments << projectPath_;
        if (!kind_.isEmpty() && !document_.isEmpty()) {
            arguments << QStringLiteral("--document") << kind_ + QLatin1Char('/') + document_;
        }
    }
    // Closing asks about unsaved edits and running tasks; kept, no restart.
    if (!close()) return;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments, QDir::currentPath());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmMovieEdits() || !confirmDiscard(false)) {
        event->ignore();
        return;
    }
    // A kit task may be writing the game's folder (preparing or resetting a
    // version): stopping it halfway is the user's call.
    if (isBusy() && !frame::MessageDialog::confirm(this, tr("Exit"), tr("A task is still running"),
                                                   tr("Exiting now stops it before it finishes."), tr("Exit"))) {
        event->ignore();
        return;
    }
    processes_->abandon();
    emulator_->stopNow();
    event->accept();
}

void MainWindow::rememberRecent(const QString& path) {
    QSettings settings;
    QStringList recent = settings.value(QString::fromLatin1(kRecentKey)).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > kMaxRecent) {
        recent.removeLast();
    }
    settings.setValue(QString::fromLatin1(kRecentKey), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    const QStringList recent = QSettings().value(QString::fromLatin1(kRecentKey)).toStringList();
    for (const QString& path : recent) {
        QAction* item = recentMenu_->addAction(recentLabel(path));
        item->setToolTip(path);
        connect(item, &QAction::triggered, this, [this, path] { openProject(path); });
    }
    recentMenu_->setEnabled(!recent.isEmpty());
}

// ---------------------------------------------------------------- navigation

void MainWindow::showDocument(const QString& kind, const QString& document) {
    if (!session_) {
        return;
    }
    selectKind(kind);
    if (!document.isEmpty()) {
        documents_->selectDocument(document);
        selectDocument(document);
    }
}

void MainWindow::selectKind(const QString& kind) {
    if (!session_) {
        return;
    }
    kind_ = kind;
    rail_->setCurrentKind(kind);
    QString label = kind;
    for (const sdk::Kind& k : session_->kinds()) {
        if (k.id == kind) {
            label = rlValue(k.label);
        }
    }
    documents_->setTitle(label);
    const QVector<sdk::DocumentGroup> groups = session_->documents(kind);
    documents_->setGroups(groups);
    QString document = lastDocument_.value(kind);
    if (document.isEmpty()) {
        for (const sdk::DocumentGroup& group : groups) {
            if (!group.documents.isEmpty()) {
                document = group.documents.first().id;
                break;
            }
        }
    }
    documents_->selectDocument(document);
    selectDocument(document);
}

void MainWindow::selectDocument(const QString& document) {
    if (!session_) {
        return;
    }
    document_ = document;
    lastDocument_.insert(kind_, document);

    inspector_->showMessage(QString(), QString(), QString());
    if (model_ && model_->type() == sdk::EditorType::Emulator && editor_) {
        editor_->hide();
        parkedEditor_ = std::move(editor_);
        parkedModel_ = std::move(model_);
    }
    editor_.reset();
    model_.reset();
    if (parkedEditor_ && kind_ == emulatorKind() && document == parkedDocument_) {
        editor_ = std::move(parkedEditor_);
        model_ = std::move(parkedModel_);
        editorStack_->setCurrentWidget(editor_.get());
        editor_->show();
        inspector_->setModel(model_.get());
        updateToolbarRow();
    } else {
        model_ = document.isEmpty() ? nullptr : session_->openEditor(kind_, document);
        installEditor();
        if (model_ && model_->type() == sdk::EditorType::Emulator) {
            parkedDocument_ = document;
        }
    }
    updateBreadcrumb();
    updateDirtyState();
    updateStatus();
}

void MainWindow::installEditor() {
    if (model_) {
        editors::EditorServices services;
        services.emulator = emulator_;
        services.processes = processes_;
        editor_ = editors::createEditorView(model_.get(), services, editorStack_);
    }
    if (editor_) {
        editorStack_->addWidget(editor_.get());
        editorStack_->setCurrentWidget(editor_.get());
        connect(editor_.get(), &editors::EditorView::toolbarChanged, this, &MainWindow::updateToolbarRow);
        connect(editor_.get(), &editors::EditorView::actionFailed, this, &MainWindow::refuseAction);
        connect(model_.get(), &sdk::EditorModel::contentChanged, this, [this] {
            updateBreadcrumb();
            updateStatus();
        });
        connect(model_.get(), &sdk::EditorModel::selectionChanged, this, [this] { updateStatus(); });
        inspector_->setModel(model_.get());
    } else {
        QString label = kind_;
        for (const sdk::Kind& k : session_->kinds()) {
            if (k.id == kind_) label = rlValue(k.label);
        }
        // A plugin that first adds documents or resources to the project
        // (a kit upgrade) opens nothing yet and asks for the document again
        // when the kit is done.
        const bool preparing = !document_.isEmpty() && isBusy();
        if (document_.isEmpty()) {
            emptyTitle_->setText(tr("Nothing to edit"));
            emptyNote_->setText(tr("This kind has no documents."));
        } else if (preparing) {
            emptyTitle_->setText(tr("Preparing %1").arg(label));
            emptyNote_->setText(tr("The kit is adding what %1 needs to the project; it opens "
                                   "when that is done (see Build output).")
                                    .arg(label));
        } else {
            emptyTitle_->setText(tr("%1 cannot be opened").arg(label));
            emptyNote_->setText(
                tr("The project has no data for this document, or the kit could not "
                   "add it. Messages says why."));
        }
        editorStack_->setCurrentWidget(emptyEditor_);
        inspector_->showMessage(label.toUpper(), document_.isEmpty() ? label : document_,
                                preparing ? tr("Waiting for the kit.") : QString());
    }
    updateToolbarRow();
}

void MainWindow::updateToolbarRow() {
    if (toolbarContent_) {
        toolbarContent_->hide();
        toolbarContent_->deleteLater();
        toolbarContent_ = nullptr;
    }
    if (editor_) {
        // A toolbar wider than the row (long translations, a narrow window)
        // scrolls with the wheel instead of squeezing its controls together.
        auto* scroller = new ToolbarScroller(toolbarRow_);
        toolbarContent_ = scroller;
        auto* content = new QWidget;
        auto* row = new QHBoxLayout(content);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(12);
        row->addWidget(editor_->createToolbar(content), 1);
        // Where "Play from selected" starts, when this document sets it.
        const QString start = model_ ? model_->startPoint() : QString();
        if (!start.isEmpty()) {
            auto* chip = new ui::ViewChip(tr("Playtest start"), start, content);
            // The game's player on it, so the start stands out in the row.
            if (session_) chip->setPicture(session_->playerIcon());
            chip->setToolTip(tr("Play from selected (F4) starts here"));
            // Where the start is put by hand, a click picks it for the Add
            // tool: it follows the cursor until a click sets it down.
            const QString item = model_->startLibraryItem();
            if (!item.isEmpty()) {
                chip->setToolTip(
                    tr("Play from selected (F4) starts here. Click, then click the map to move it, or drag it "
                       "onto the map."));
                chip->setDragData(QString::fromLatin1(editors::kLibraryItemMime), item.toUtf8());
                connect(chip, &QAbstractButton::clicked, this, [this, item] {
                    if (editor_) editor_->armPlacement(item);
                });
            }
            row->addWidget(chip);
        }
        for (const sdk::EditorAction& action : model_ ? model_->actions() : QVector<sdk::EditorAction>()) {
            auto* button = new ui::PrimaryButton(action.label, action.icon, content);
            button->setDanger(action.danger);
            button->setToolTip(action.tooltip);
            connect(button, &QAbstractButton::clicked, this, [this, action] {
                if (!action.confirmHeading.isEmpty() &&
                    !frame::MessageDialog::confirm(
                        this, action.label, action.confirmHeading, action.confirmText,
                        action.confirmAction.isEmpty() ? action.label : action.confirmAction)) {
                    return;
                }
                // The model acts before the Playtest view replaces its editor.
                if (model_) model_->triggerAction(action.id);
                if (action.showsPlaytest) showPlaytest();
            });
            row->addWidget(button);
        }
        scroller->setWidget(content);
        toolbarRowLayout_->addWidget(toolbarContent_);
    }
    toolbarRow_->setVisible(editor_ != nullptr);
}

// ---------------------------------------------------------------- state

void MainWindow::updateProjectChrome() {
    if (!session_) {
        platformBadge_->setText(QStringLiteral("—"));
        projectName_->setText(tr("No project"));
        revisionBadge_->hide();
        setWindowTitle(tr("Retrolarium"));
        titleBar()->setTitle(tr("Retrolarium Studio"));
        projectButton_->setMinimumWidth(projectButton_->layout()->sizeHint().width());
        closeAction_->setEnabled(false);
        switchRevisionAction_->setEnabled(false);
        resetAction_->setEnabled(false);
        patchAction_->setEnabled(false);
        return;
    }
    const sdk::Profile profile = session_->profile();
    platformBadge_->setText(profile.badge);
    projectName_->setText(profile.name);
    revisionBadge_->setText(profile.revision);
    revisionBadge_->setVisible(!profile.revision.isEmpty());
    projectButton_->setMinimumWidth(projectButton_->layout()->sizeHint().width());
    const QString title = profile.revision.isEmpty()
                              ? QStringLiteral("%1 [%2]").arg(profile.name, profile.platform)
                              : QStringLiteral("%1 (%2) [%3]").arg(profile.name, profile.revision, profile.platform);
    setWindowTitle(title + tr(" — Retrolarium"));
    titleBar()->setTitle(title, tr("Retrolarium"));
    closeAction_->setEnabled(true);
    switchRevisionAction_->setEnabled(!currentDescriptor().isEmpty());
    resetAction_->setEnabled(switchRevisionAction_->isEnabled());
    // An IPS patch needs a ROM base: every project but a source build.
    QString error;
    const QString mode = sdk::ProjectFile::load(projectPath_, &error).string(QStringLiteral("project.source_mode"));
    patchAction_->setEnabled(mode == QLatin1String("rom") || mode == QLatin1String("binary-profile") ||
                             mode == QLatin1String("flicky-content"));
}

void MainWindow::updateBreadcrumb() {
    if (!session_) {
        return;
    }
    QString kindLabel = kind_;
    for (const sdk::Kind& k : session_->kinds()) {
        if (k.id == kind_) {
            kindLabel = rlValue(k.label);
        }
    }
    crumbKind_->setText(kindLabel);
    QString documentLabel = document_;
    bool dirty = false;
    for (const sdk::DocumentGroup& group : session_->documents(kind_)) {
        for (const sdk::DocumentEntry& entry : group.documents) {
            if (entry.id == document_) {
                documentLabel = entry.label;
                dirty = entry.dirty;
            }
        }
    }
    crumbDocument_->setText(documentLabel);
    crumbEdited_->setVisible(dirty);
    crumbEditor_->setVisible(model_ != nullptr);
    if (model_) {
        crumbEditor_->setText(sdk::editorName(model_->type()));
        const sdk::Budget budget = model_->budget();
        budgetText_->setText(budget.text);
        budgetText_->setVisible(budget.isValid());
        budgetBar_->setVisible(budget.capacity > 0);
        budgetBar_->setBudget(budget.used, budget.capacity);
    } else {
        budgetText_->hide();
        budgetBar_->hide();
    }
    updateActions();
}

void MainWindow::scheduleDirtyState() {
    if (!dirtyTimer_) {
        dirtyTimer_ = new QTimer(this);
        dirtyTimer_->setSingleShot(true);
        dirtyTimer_->setInterval(0);
        connect(dirtyTimer_, &QTimer::timeout, this, &MainWindow::updateDirtyState);
    }
    dirtyTimer_->start();
}

void MainWindow::updateDirtyState() {
    const bool open = session_ != nullptr;
    QUndoStack* undo = tasEditorShown() ? tasEditor_->undoStack() : open ? session_->undoStack() : nullptr;
    undoAction_->setEnabled(undo && undo->canUndo());
    redoAction_->setEnabled(undo && undo->canRedo());
    undoButton_->setEnabled(undoAction_->isEnabled());
    redoButton_->setEnabled(redoAction_->isEnabled());
    undoButton_->setToolTip(undo && undo->canUndo() ? tr("Undo %1 (Ctrl+Z)").arg(undo->undoText())
                                                    : tr("Undo (Ctrl+Z)"));
    redoButton_->setToolTip(undo && undo->canRedo() ? tr("Redo %1 (Ctrl+Y)").arg(undo->redoText())
                                                    : tr("Redo (Ctrl+Y)"));
    const bool modified = open && session_->isModified();
    const bool movieModified = tasEditor_ && tasEditor_->isModified();
    saveAction_->setEnabled(modified || movieModified);
    saveButton_->setEnabled(modified || movieModified);
    revertButton_->setEnabled(open && session_->canRevert(kind_, document_));

    int dirty = 0;
    if (open) {
        for (const sdk::Kind& kind : session_->kinds()) {
            for (const sdk::DocumentGroup& group : session_->documents(kind.id)) {
                for (const sdk::DocumentEntry& entry : group.documents) {
                    dirty += entry.dirty;
                }
            }
        }
    }
    if (modified && dirty == 0) {
        dirty = 1;
    }
    updateActions();
    dirtyIndicator_->setVisible(modified);
    dirtyLabel_->setText(tr("%n unsaved documents", nullptr, dirty));
    updateBreadcrumb();
}

void MainWindow::updateStatus(const QString& transient) {
    if (!session_) {
        statusLeft_->setText(tr("No project open"));
        statusRight_->clear();
        return;
    }
    QString left = transient;
    if (left.isEmpty() && model_) {
        left = model_->statusText();
    }
    statusLeft_->setText(left);
    const sdk::Profile profile = session_->profile();
    statusRight_->setText(
        tr("%1 · %2 · undo %3").arg(profile.platform, profile.cpu).arg(session_->undoStack()->index()));
}

void MainWindow::refreshDiagnostics() {
    QVector<sdk::Diagnostic> all = hostMessages_;
    if (session_) {
        all += session_->diagnostics();
    }
    bottom_->setDiagnostics(all);
}

void MainWindow::saveAll() {
    if (!session_) {
        return;
    }
    QString error;
    if (session_->save(&error)) {
        updateStatus(tr("Saved"));
    } else {
        report(message(sdk::Severity::Error, QStringLiteral("save"), error, QString()));
    }
    if (tasEditor_ && tasEditor_->isModified()) tasEditor_->save();
    updateDirtyState();
    documents_->updateGroups(session_->documents(kind_));
}

void MainWindow::revertDocument() {
    if (!session_ || !session_->canRevert(kind_, document_)) {
        return;
    }
    if (!frame::MessageDialog::confirm(
            this, tr("Revert document"), tr("Revert %1?").arg(crumbDocument_->text()),
            tr("It returns to the state it had when the project was created. This can be undone."), tr("Revert"))) {
        return;
    }
    QString error;
    if (!session_->revert(kind_, document_, &error)) {
        report(message(sdk::Severity::Error, QStringLiteral("revert"), error, crumbDocument_->text()));
    }
}

// ---------------------------------------------------------------- host services

void MainWindow::runProcess(const sdk::ProcessSpec& spec, std::function<void(const sdk::ProcessResult&)> done) {
    processes_->run(spec, std::move(done));
}

bool MainWindow::isBusy() const { return processes_->isBusy(); }

void MainWindow::cancel() { processes_->cancel(); }

sdk::ProcessSpec MainWindow::kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                        QString* error) const {
    return runtime::ToolLocator::kitCommand(arguments, workingDirectory, error);
}

QString MainWindow::toolPath(const QString& name) const { return runtime::ToolLocator::toolPath(name); }

QString MainWindow::emulatorPath(const QString& id) const { return runtime::ToolLocator::emulatorPath(id); }

QString MainWindow::emulatorKind() const {
    if (!session_) {
        return {};
    }
    for (const sdk::Kind& kind : session_->kinds()) {
        if (kind.editor == sdk::EditorType::Emulator) {
            return kind.id;
        }
    }
    return {};
}

// Play opens the Playtest view at once, so the build that comes first shows
// there too.
void MainWindow::showPlaytest() {
    const QString kind = emulatorKind();
    if (!kind.isEmpty() && kind_ != kind) {
        selectKind(kind);
    }
}

void MainWindow::stopEmulator() { emulator_->stop(); }

void MainWindow::launchEmulator(const sdk::ProcessSpec& spec, const QString& title,
                                std::function<void(const sdk::ProcessResult&)> finished) {
    const QString kind = emulatorKind();
    if (kind.isEmpty()) {
        sdk::ProcessResult result;
        result.error = tr("This project has no Playtest view.");
        if (finished) {
            finished(result);
        }
        return;
    }
    // The Playtest view owns the surface the emulator draws into.
    showPlaytest();
    log(QStringLiteral("▸ %1").arg(title));
    log(QStringLiteral("  %1").arg(runtime::displayCommand(spec)));
    emulator_->start(spec, title, std::move(finished));
}

void MainWindow::playAudio(const QString& wavPath) { audio_->play(wavPath); }

void MainWindow::stopAudio() { audio_->stop(); }

bool MainWindow::audioLoops() const { return audio_->isLooping(); }

void MainWindow::setAudioLoop(bool loop) { audio_->setLooping(loop); }

void MainWindow::updateActions() {
    const bool open = session_ != nullptr;
    const bool busy = isBusy();
    const bool canBuild = open && session_->canBuild();
    const bool canPlay = open && session_->canPlay();
    const QString start = canPlay ? session_->selectedStart() : QString();
    buildAction_->setEnabled(canBuild && !busy);
    playAction_->setEnabled(canPlay && !busy);
    const QString label = canPlay ? session_->playSelectedLabel() : QString();
    playSelectedAction_->setEnabled(!start.isEmpty() && !busy);
    playSelectedAction_->setText(!label.isEmpty()  ? label
                                 : start.isEmpty() ? tr("Play from selected")
                                                   : tr("Play from %1").arg(start));
    playSelectedButton_->setText(label.isEmpty() ? tr("Play from selected") : label);
    buildButton_->setEnabled(canBuild || busy);
    buildButton_->setText(busy ? tr("Cancel") : tr("Build"));
    playButton_->setEnabled(canPlay && !busy);
    playSelectedButton_->setEnabled(!start.isEmpty() && !busy);
    playSelectedButton_->setToolTip(start.isEmpty() ? tr("Pick a start point in an editor (F4)")
                                                    : tr("Save, build and play from %1 (F4)").arg(start));

    using Phase = runtime::EmulatorRunner::Phase;
    const Phase phase = emulator_->phase();
    const bool running = phase == Phase::Running;
    const bool live = running || phase == Phase::Starting;
    const bool paused = emulator_->isPaused();
    pauseGameAction_->setEnabled(running);
    pauseGameAction_->setText(paused ? tr("Resume") : tr("Pause"));
    pauseButton_->setEnabled(running);
    pauseButton_->setIcon(theme::icon(paused ? QStringLiteral("play") : QStringLiteral("pause")));
    const bool tas = tasEditorShown();
    if (tas) {
        pauseButton_->setToolTip(paused ? tr("Play the movie from the selected frame (Pause, Space)")
                                        : tr("Pause the movie where it is (Pause, Space)"));
        stopButton_->setToolTip(tr("Pause and go back to the selected frame (Shift+F5)"));
    } else {
        pauseButton_->setToolTip(paused ? tr("Resume the playtest (Pause)") : tr("Pause the playtest (Pause)"));
        stopButton_->setToolTip(tr("Stop the playtest (Shift+F5)"));
    }
    stopGameAction_->setEnabled(live);
    stopGameAction_->setText(tas ? tr("Stop at the selected frame") : tr("Stop"));
    stopButton_->setEnabled(live);
    const int frame = tas ? tasEditor_->selectedRow() : -1;
    cutFrameAction_->setEnabled(frame >= 0);
    copyFrameAction_->setEnabled(frame >= 0);
    pasteFrameAction_->setEnabled(tas && tasEditor_->canPaste());
    insertFrameAction_->setEnabled(tas);
    deleteFrameAction_->setEnabled(frame >= 0);

    const bool controls = running && emulator_->hasControls();
    frameAdvanceAction_->setEnabled(controls);
    saveStateAction_->setEnabled(controls);
    loadStateAction_->setEnabled(controls);
    previousSlotAction_->setEnabled(controls);
    nextSlotAction_->setEnabled(controls);
    saveStateAction_->setText(tr("Save state (slot %1)").arg(emulator_->stateSlot()));
    loadStateAction_->setText(tr("Load state (slot %1)").arg(emulator_->stateSlot()));
    for (QAction* speed : speedGroup_->actions()) {
        speed->setChecked(speed->data().toInt() == emulator_->speed());
    }
    const bool movies = open && !emulatorKind().isEmpty() && !busy;
    recordMovieAction_->setEnabled(movies);
    resumeMovieAction_->setEnabled(movies);
    playMovieAction_->setEnabled(movies);
    stopMovieAction_->setEnabled(running && emulator_->movieMode() != QLatin1String("none"));
}

void MainWindow::togglePlaytestPause() {
    if (emulator_->phase() != runtime::EmulatorRunner::Phase::Running) return;
    if (tasEditorShown()) {
        tasEditor_->playOrPause();
        return;
    }
    if (!emulator_->togglePause()) {
        report(message(sdk::Severity::Warning, QStringLiteral("playtest"), tr("The emulator could not be paused."),
                       QString()));
        return;
    }
    notifyEmulation(emulator_->isPaused() ? tr("Paused") : tr("Resumed"));
}

void MainWindow::setEmulationSpeed(int percent) {
    emulator_->setSpeed(percent);
    // The ends of the scale say so.
    const int slowest = kSpeeds[0];
    if (percent == 0) {
        notifyEmulation(tr("Speed: no limit"));
    } else {
        notifyEmulation(percent == slowest ? tr("Speed %1% (slowest)").arg(percent) : tr("Speed %1%").arg(percent));
    }
}

void MainWindow::stepStateSlot(int direction) {
    emulator_->setStateSlot(emulator_->stateSlot() + direction);
    const int slot = emulator_->stateSlot();
    const int frame = emulator_->slotFrame(slot);
    if (!emulator_->slotHasState(slot)) {
        notifyEmulation(tr("State %1 selected (empty)").arg(slot));
    } else if (frame < 0 || emulator_->movieMode() == QLatin1String("none")) {
        // A time is a movie's: outside one it would say nothing.
        notifyEmulation(tr("State %1 selected (used)").arg(slot));
    } else {
        // When in the movie it was saved.
        const int seconds = int(frame / 60.0 + 0.5);
        notifyEmulation(tr("State %1 selected (used, %2)")
                            .arg(slot)
                            .arg(QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'))));
    }
}

// Over the game's picture, as FCEUX shows it; a toast when no game runs.
void MainWindow::notifyEmulation(const QString& text) {
    if (!emulator_->showMessage(text) && toast_) toast_->showMessage(text);
}

void MainWindow::stepEmulationSpeed(int direction) {
    // No limit counts as the fastest step.
    const int count = int(std::size(kSpeeds));
    int index = int(std::find(std::begin(kSpeeds), std::end(kSpeeds), emulator_->speed()) - std::begin(kSpeeds));
    if (index >= count) index = int(std::find(std::begin(kSpeeds), std::end(kSpeeds), 100) - std::begin(kSpeeds));
    const int next = std::clamp(index + direction, 0, count - 1);
    if (next == index && kSpeeds[next] == 0) {
        emulator_->setSpeed(0);
        notifyEmulation(tr("Speed: no limit (fastest)"));
        return;
    }
    setEmulationSpeed(kSpeeds[next]);
}

void MainWindow::showEmulationEvent(const QString& event) {
    const int slot = emulator_->stateSlot();
    QString text;
    // Saved and loaded were shown when asked for.
    if (event == QLatin1String("saved") || event == QLatin1String("loaded")) {
        return;
    } else if (event == QLatin1String("no-state")) {
        text = tr("State %1 is empty").arg(slot);
    } else if (event == QLatin1String("ended")) {
        text = tr("Movie ended: you have control");
    } else if (event == QLatin1String("stopped")) {
        text = emulator_->movie().mode == runtime::MovieRequest::Mode::Play ? tr("Movie stopped")
                                                                            : tr("Movie stopped and saved");
    } else if (event == QLatin1String("recording")) {
        text = tr("End of movie: paused, recording on (Pause to go on)");
    } else if (event == QLatin1String("rewound")) {
        text = tr("Frame %1").arg(emulator_->movieFrame());
    } else if (event == QLatin1String("rewind-limit")) {
        text = tr("Cannot rewind further");
    } else if (event == QLatin1String("foreign-state")) {
        text = tr("State %1 is not from this movie: movie stopped").arg(slot);
    } else if (event == QLatin1String("movie-end")) {
        text = tr("End of the movie");
    } else if (event == QLatin1String("movie-not-saved")) {
        text = tr("The movie could not be written: the file keeps its last save");
    }
    if (text.isEmpty()) return;
    notifyEmulation(text);
    log(QStringLiteral("  %1").arg(text));
}

namespace {

// A recording's file in its folder: the title as a file name, numbered when
// it is taken.
QString movieFileName(const QString& folder, const QString& title) {
    QString stem = title.toLower();
    stem.replace(QRegularExpression(QStringLiteral("[^\\w]+"), QRegularExpression::UseUnicodePropertiesOption),
                 QStringLiteral("-"));
    stem.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (stem.isEmpty()) stem = QStringLiteral("movie");
    QString path = QDir(folder).filePath(stem + QStringLiteral(".rlmovie"));
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(QStringLiteral("%1-%2.rlmovie").arg(stem).arg(n));
    }
    return path;
}

// A Retrolarium movie's header lines (its description), without the frame
// count the recording writes itself; empty when it is not a movie recorded
// in the studio, which cannot be resumed.
QString studioMovieHeader(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString header;
    bool studio = false;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    if (lines.value(0).trimmed() != QLatin1String("# retrolarium movie 1")) return {};
    for (int i = 1; i < lines.size() && lines.at(i).trimmed() != QLatin1String("---"); ++i) {
        const QString line = lines.at(i).trimmed();
        // Frames skipped at the start (an offset) are the kit's to line up:
        // the recording would go on from the wrong frame.
        if (line.startsWith(QLatin1String("offset ")) && line.mid(7).trimmed().toInt() != 0) return {};
        if (line.startsWith(QLatin1String("frames ")) || line.startsWith(QLatin1String("offset "))) continue;
        // MAME's own, or a port to MAME of another emulator's ("source mame
        // from fm2 ..."): both go on in MAME.
        if (line == QLatin1String("source mame") || line.startsWith(QLatin1String("source mame "))) studio = true;
        header += line + QLatin1Char('\n');
    }
    return studio ? header : QString();
}

QVector<MovieChoice> movieChoices(const QJsonArray& movies) {
    QVector<MovieChoice> choices;
    for (const QJsonValue& value : movies) {
        const QJsonObject m = value.toObject();
        MovieChoice choice;
        choice.path = m.value(QStringLiteral("path")).toString();
        choice.file = m.value(QStringLiteral("file")).toString();
        choice.title = m.value(QStringLiteral("title")).toString();
        choice.kind = m.value(QStringLiteral("kind")).toString();
        choice.author = m.value(QStringLiteral("author")).toString();
        choice.description = m.value(QStringLiteral("description")).toString();
        choice.source = m.value(QStringLiteral("source")).toString();
        choice.frames = m.value(QStringLiteral("frames")).toInt();
        choice.seconds = m.value(QStringLiteral("seconds")).toDouble();
        choices << choice;
    }
    return choices;
}

}  // namespace

void MainWindow::startMovie(int mode) {
    using Mode = runtime::MovieRequest::Mode;
    if (!session_ || emulatorKind().isEmpty() || isBusy()) return;
    // Another run replaces the movie the TAS editor edits.
    if (!confirmMovieEdits()) return;
    const QString mame = runtime::ToolLocator::emulatorPath(QStringLiteral("mame"));
    if (mame.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("playtest"),
                       tr("MAME is not found: choose it in Settings."), QString()));
        return;
    }
    // A game repository's version runs its own ROM on its own machine and
    // keeps its movies in the repository; any other project its ROM, with
    // the movies the studio keeps for that image.
    QStringList arguments{QStringLiteral("movie"), QStringLiteral("plan"), QStringLiteral("--mame"), mame};
    const QString descriptor = currentDescriptor();
    QString directory;
    if (!descriptor.isEmpty()) {
        arguments << QStringLiteral("--descriptor") << descriptor << QStringLiteral("--revision")
                  << QFileInfo(projectPath_).absoluteDir().dirName();
        directory = QFileInfo(descriptor).absolutePath();
    } else {
        QString error;
        const sdk::ProjectFile project = sdk::ProjectFile::load(projectPath_, &error);
        const QString rom =
            QFileInfo(projectPath_).absoluteDir().absoluteFilePath(project.string(QStringLiteral("source.rom")));
        arguments << QStringLiteral("--rom") << rom << QStringLiteral("--movies-root")
                  << QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                         .filePath(QStringLiteral("movies"));
        directory = QFileInfo(projectPath_).absolutePath();
    }
    QString error;
    sdk::ProcessSpec spec = kitCommand(arguments, directory, &error);
    if (spec.program.isEmpty()) {
        report(message(sdk::Severity::Error, QStringLiteral("playtest"), error, QString()));
        return;
    }
    spec.label = tr("Find the movies");
    const QString game = session_->profile().name;
    runProcess(spec, [this, mode, arguments, directory, game](const sdk::ProcessResult& result) {
        if (!result.succeeded()) {
            if (!result.cancelled) {
                report(message(sdk::Severity::Error, QStringLiteral("playtest"),
                               tr("The movie cannot start: %1").arg(processError(result)), QString()));
            }
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(result.standardOutput).object();
        const QString folder = answer.value(QStringLiteral("movies_dir")).toString();
        if (mode == int(Mode::Record)) {
            RecordMovieDialog dialog(game, QDir::toNativeSeparators(folder), this);
            if (dialog.exec() != QDialog::Accepted) return;
            const MovieDescription description = dialog.description();
            QDir().mkpath(folder);
            QString header =
                QStringLiteral("platform %1\nsource mame\nrom %2\n")
                    .arg(answer.value(QStringLiteral("platform")).toString(),
                         answer.value(QStringLiteral("rom")).toObject().value(QStringLiteral("sha1")).toString());
            const QList<QPair<QString, QString>> fields{{QStringLiteral("title"), description.title},
                                                        {QStringLiteral("kind"), description.kind},
                                                        {QStringLiteral("author"), description.author},
                                                        {QStringLiteral("description"), description.description}};
            for (const auto& field : fields) {
                QString value = field.second;
                value.remove(QStringLiteral("]==]"));
                if (!value.isEmpty()) header += QStringLiteral("%1 %2\n").arg(field.first, value);
            }
            launchMovie(answer, mode, movieFileName(folder, description.title), header, 0, false);
            return;
        }
        const auto purpose =
            mode == int(Mode::Play) ? PickMovieDialog::Purpose::Play : PickMovieDialog::Purpose::Resume;
        PickMovieDialog dialog(purpose, game, movieChoices(answer.value(QStringLiteral("movies")).toArray()), this);
        if (dialog.exec() != QDialog::Accepted || dialog.path().isEmpty()) {
            openEditorOnLaunch_ = false;
            return;
        }
        const QString path = dialog.path();
        QString header;
        if (mode == int(Mode::Resume)) {
            header = studioMovieHeader(path);
            if (header.isEmpty()) {
                report(message(
                    sdk::Severity::Error, QStringLiteral("playtest"),
                    tr("%1 was not recorded in the studio: it can play, not go on.").arg(QFileInfo(path).fileName()),
                    QString()));
                return;
            }
        }
        // Played as MAME plays it: the kit lines another emulator's movie up
        // with MAME's frames and power-on state. A recording goes on in its
        // own file, from the power-on state the kit gives it.
        const QString prepared = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                     .filePath(QStringLiteral("movies/playing.rlmovie"));
        QDir().mkpath(QFileInfo(prepared).absolutePath());
        QString error;
        sdk::ProcessSpec again =
            kitCommand(arguments + QStringList{QStringLiteral("--movie"), path, QStringLiteral("--prepared"), prepared},
                       directory, &error);
        again.label = tr("Prepare %1").arg(QFileInfo(path).fileName());
        const int stopAt = dialog.stopAt();
        const bool closeAtEnd = dialog.closeAtEnd();
        runProcess(again, [this, mode, prepared, path, header, stopAt, closeAtEnd](const sdk::ProcessResult& played) {
            if (!played.succeeded()) {
                if (!played.cancelled) {
                    report(message(sdk::Severity::Error, QStringLiteral("playtest"),
                                   tr("%1 cannot play: %2").arg(QFileInfo(path).fileName(), processError(played)),
                                   QString()));
                }
                return;
            }
            const QJsonObject planned = QJsonDocument::fromJson(played.standardOutput).object();
            if (mode == int(Mode::Resume)) {
                launchMovie(planned, mode, path, header, 0, false);
            } else {
                launchMovie(planned, mode, prepared, QString(), stopAt, closeAtEnd, path);
            }
        });
    });
}

void MainWindow::launchMovie(const QJsonObject& answer, int mode, const QString& file, const QString& header,
                             int stopAt, bool closeAtEnd, const QString& origin) {
    using Mode = runtime::MovieRequest::Mode;
    if (emulatorKind().isEmpty()) return;
    runtime::MovieRequest movie;
    movie.mode = Mode(mode);
    movie.file = file;
    movie.origin = origin;
    movie.header = header;
    movie.stateKey = answer.value(QStringLiteral("rom")).toObject().value(QStringLiteral("sha1")).toString();
    movie.stopAt = stopAt;
    movie.closeAtEnd = closeAtEnd;
    const QJsonObject plan = answer.value(QStringLiteral("plan")).toObject();
    sdk::ProcessSpec spec = sdk::specFromPlan(plan);
    // A console NES in the studio's colours, as the game's own playtest has
    // it (a Vs. System board keeps its palette).
    static const QStringList consoles{QStringLiteral("nes"), QStringLiteral("nespal"), QStringLiteral("famicom"),
                                      QStringLiteral("fds")};
    if (answer.value(QStringLiteral("platform")).toString() == QLatin1String("nes") &&
        consoles.contains(plan.value(QStringLiteral("machine")).toString())) {
        for (int i = 0; i < 64; ++i) spec.palette << QRgb(nes::masterColor(i));
    }
    const QString name = QFileInfo(file).fileName();
    const QString title = mode == int(Mode::Record)   ? tr("Recording %1").arg(name)
                          : mode == int(Mode::Resume) ? tr("Resuming %1").arg(name)
                                                      : tr("Playing a movie");
    showPlaytest();
    log(QStringLiteral("▸ %1").arg(title));
    log(QStringLiteral("  %1").arg(runtime::displayCommand(spec)));
    const bool recording = mode != int(Mode::Play);
    emulator_->start(
        spec, title,
        [this, recording, file](const sdk::ProcessResult&) {
            if (recording && QFileInfo::exists(file)) {
                const QString text = tr("Movie saved: %1").arg(QDir::toNativeSeparators(file));
                if (toast_) toast_->showMessage(text);
                report(message(sdk::Severity::Info, QStringLiteral("playtest"), text, QFileInfo(file).fileName()));
            }
        },
        movie);
    // The movie's title over the picture as it starts.
    QString movieTitle = QFileInfo(file).completeBaseName();
    QFile text(file);
    const QString lines = !header.isEmpty()                ? header
                          : text.open(QIODevice::ReadOnly) ? QString::fromUtf8(text.read(4096))
                                                           : QString();
    for (const QString& line : lines.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("title "))) movieTitle = line.mid(6).trimmed();
    }
    notifyEmulation(recording ? tr("Recording movie «%1»").arg(movieTitle) : tr("Playing movie «%1»").arg(movieTitle));
    // The TAS editor shows when it was asked for, not for every movie.
    if (openEditorOnLaunch_ && mode == int(Mode::Play)) {
        openTasEditor();
    } else {
        closeTasEditor();
    }
    openEditorOnLaunch_ = false;
}

void MainWindow::openTasEditor() {
    if (tasEditor_ == nullptr) {
        tasEditor_ = new TasEditorWindow(emulator_, this);
        tasEditor_->hide();
        // Its undo, selection and copied frame: the menus follow.
        connect(tasEditor_, &TasEditorWindow::changed, this, &MainWindow::scheduleDirtyState);
    }
    QString error;
    if (!tasEditor_->open(emulator_->movie().file, emulator_->movie().origin, &error)) {
        report(message(sdk::Severity::Error, QStringLiteral("playtest"),
                       tr("The TAS editor cannot open the movie: %1").arg(error), QString()));
        return;
    }
    // Docked left of the picture in the Playtest view, brought forward.
    if (!qobject_cast<editors::EmulatorView*>(editor_.get()) && parkedEditor_) selectKind(emulatorKind());
    auto* playtest = qobject_cast<editors::EmulatorView*>(editor_.get());
    if (!playtest) {
        report(message(sdk::Severity::Warning, QStringLiteral("playtest"),
                       tr("The TAS editor opens beside the Playtest picture: open Playtest first."), QString()));
        return;
    }
    playtest->setSidePanel(tasEditor_);
    tasEditor_->setFocus();
    scheduleDirtyState();
}

void MainWindow::closeTasEditor() {
    if (tasEditor_ == nullptr) return;
    for (editors::EditorView* view : {editor_.get(), parkedEditor_.get()}) {
        auto* playtest = qobject_cast<editors::EmulatorView*>(view);
        if (playtest && playtest->sidePanel() == tasEditor_) playtest->setSidePanel(nullptr);
    }
    tasEditor_->hide();
    scheduleDirtyState();
}

bool MainWindow::tasEditorShown() const { return tasEditor_ != nullptr && tasEditor_->isVisible(); }

}  // namespace rl::shell
