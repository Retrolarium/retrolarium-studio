#include "shell/main_window.hpp"

#include "editors/editor_view.hpp"
#include "frame/studio_dialog.hpp"
#include "frame/title_bar.hpp"
#include "runtime/audio_player.hpp"
#include "runtime/emulator_runner.hpp"
#include "runtime/process_runner.hpp"
#include "runtime/tool_locator.hpp"
#include "sdk/editor_model.hpp"
#include "shell/about_dialog.hpp"
#include "shell/bottom_panel.hpp"
#include "shell/inspector_panel.hpp"
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
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace rl::shell {

namespace {

constexpr int kMaxRecent = 8;
const char* kRecentKey = "recentProjects";

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

sdk::Diagnostic message(sdk::Severity severity, const QString& source, const QString& text,
                        const QString& location) {
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

}  // namespace

MainWindow::MainWindow(QWidget* parent) : StudioWindow(parent) {
    processes_ = new runtime::ProcessRunner(this);
    emulator_ = new runtime::EmulatorRunner(this);
    audio_ = new runtime::AudioPlayer(this);
    setWindowTitle(QStringLiteral("Retrolarium"));
    titleBar()->setTitle(QStringLiteral("Retrolarium Studio"));
    setMinimumSize(1280, 780);
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
    connect(inspector_, &InspectorPanel::fieldRejected, this, [this](const QString& message) {
        updateStatus(message);
        if (toast_) toast_->showMessage(message);
    });
    connect(bottom_, &BottomPanel::summaryChanged, this, [this](const QString& summary) {
        statusSummary_->setText(summary);
    });
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

    connect(processes_, &runtime::ProcessRunner::started, this,
            [this](const QString& label, const QString& command) {
                bottom_->showBuildOutput();
                log(QStringLiteral("▸ %1").arg(label));
                log(QStringLiteral("  %1").arg(command));
                updateActions();
            });
    connect(processes_, &runtime::ProcessRunner::outputLine, this, &MainWindow::log);
    connect(processes_, &runtime::ProcessRunner::finished, this,
            [this](const QString& label, const sdk::ProcessResult& result) {
                if (result.succeeded()) {
                    log(QStringLiteral("  %1 finished.").arg(label));
                } else if (result.cancelled) {
                    log(QStringLiteral("  %1 cancelled.").arg(label));
                } else if (!result.started) {
                    log(QStringLiteral("  %1").arg(result.error));
                } else {
                    log(QStringLiteral("  %1 failed with exit code %2.").arg(label).arg(result.exitCode));
                }
                updateActions();
            });
    connect(emulator_, &runtime::EmulatorRunner::stateChanged, this, &MainWindow::updateActions);

    closeProject();
}

MainWindow::~MainWindow() {
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

    QMenu* file = bar->addMenu(QStringLiteral("File"));
    file->setFont(menuFont);
    QAction* open = file->addAction(QStringLiteral("Open project…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::chooseProject);
    recentMenu_ = file->addMenu(QStringLiteral("Recent projects"));
    recentMenu_->setFont(menuFont);
    file->addSeparator();
    saveAction_ = file->addAction(QStringLiteral("Save all"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveAll);
    closeAction_ = file->addAction(QStringLiteral("Close project"));
    connect(closeAction_, &QAction::triggered, this, [this] {
        if (confirmDiscard()) {
            closeProject();
        }
    });
    file->addSeparator();
    QAction* quit = file->addAction(QStringLiteral("Exit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* edit = bar->addMenu(QStringLiteral("Edit"));
    edit->setFont(menuFont);
    undoAction_ = edit->addAction(QStringLiteral("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = edit->addAction(QStringLiteral("Redo"));
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    connect(undoAction_, &QAction::triggered, this, [this] {
        if (session_) session_->undoStack()->undo();
    });
    connect(redoAction_, &QAction::triggered, this, [this] {
        if (session_) session_->undoStack()->redo();
    });

    QMenu* view = bar->addMenu(QStringLiteral("View"));
    view->setFont(menuFont);
    QAction* dark = view->addAction(QStringLiteral("Dark theme"));
    QAction* light = view->addAction(QStringLiteral("Light theme"));
    dark->setCheckable(true);
    light->setCheckable(true);
    auto* themes = new QActionGroup(view);
    themes->addAction(dark);
    themes->addAction(light);
    (theme::Theme::instance().mode() == theme::Mode::Dark ? dark : light)->setChecked(true);
    connect(dark, &QAction::triggered, this, [] { theme::Theme::instance().setMode(theme::Mode::Dark); });
    connect(light, &QAction::triggered, this, [] { theme::Theme::instance().setMode(theme::Mode::Light); });
    connect(&theme::Theme::instance(), &theme::Theme::changed, view, [dark, light](theme::Mode mode) {
        (mode == theme::Mode::Dark ? dark : light)->setChecked(true);
    });
    QAction* toggleTheme = view->addAction(QStringLiteral("Toggle theme"));
    toggleTheme->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(toggleTheme, &QAction::triggered, this, [] { theme::Theme::instance().toggle(); });
    view->addSeparator();
    QAction* panel = view->addAction(QStringLiteral("Messages panel"));
    panel->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(panel, &QAction::triggered, this, [this] { bottom_->setExpanded(!bottom_->isExpanded()); });

    // The toolbar's buttons, in the same order.
    QMenu* build = bar->addMenu(QStringLiteral("Run"));
    build->setFont(menuFont);
    buildAction_ = build->addAction(QStringLiteral("Build"));
    buildAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(buildAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canBuild() && !isBusy()) session_->build();
    });
    playAction_ = build->addAction(QStringLiteral("Play"));
    playAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+F5")));
    connect(playAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canPlay() && !isBusy()) {
            showPlaytest();
            session_->play();
        }
    });
    playSelectedAction_ = build->addAction(QStringLiteral("Play from selected"));
    playSelectedAction_->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(playSelectedAction_, &QAction::triggered, this, [this] {
        if (session_ && session_->canPlay() && !session_->selectedStart().isEmpty() && !isBusy()) {
            // The session reads its start before the Playtest view replaces
            // the editor that sets it.
            session_->playSelected();
            showPlaytest();
        }
    });
    build->addSeparator();
    pauseGameAction_ = build->addAction(QStringLiteral("Pause"));
    pauseGameAction_->setShortcut(QKeySequence(Qt::Key_Pause));
    connect(pauseGameAction_, &QAction::triggered, this, &MainWindow::togglePlaytestPause);
    stopGameAction_ = build->addAction(QStringLiteral("Stop"));
    stopGameAction_->setShortcut(QKeySequence(QStringLiteral("Shift+F5")));
    connect(stopGameAction_, &QAction::triggered, this, [this] { emulator_->stop(); });
    pauseGameAction_->setEnabled(false);
    stopGameAction_->setEnabled(false);

    QMenu* help = bar->addMenu(QStringLiteral("Help"));
    help->setFont(menuFont);
    QAction* about = help->addAction(QStringLiteral("About Retrolarium Studio"));
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
    projectButton_->setToolTip(QStringLiteral("Switch project"));
    auto* projectLayout = new QHBoxLayout(projectButton_);
    projectLayout->setContentsMargins(6, 0, 10, 0);
    projectLayout->setSpacing(10);
    platformBadge_ = new ui::Badge(QStringLiteral("—"), ui::Badge::Kind::Platform, projectButton_);
    projectName_ = new ui::Label(QStringLiteral("No project"), ui::Label::Role::Project, projectButton_);
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
        QAction* open = menu.addAction(QStringLiteral("Open project…"));
        connect(open, &QAction::triggered, this, &MainWindow::chooseProject);
        const QStringList recent = QSettings().value(QString::fromLatin1(kRecentKey)).toStringList();
        if (!recent.isEmpty()) {
            menu.addSeparator();
            for (const QString& path : recent) {
                QAction* item = menu.addAction(QFileInfo(path).fileName());
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
    undoButton_ = toolbarButton(QStringLiteral("undo"), QStringLiteral("Undo (Ctrl+Z)"), bar);
    redoButton_ = toolbarButton(QStringLiteral("redo"), QStringLiteral("Redo (Ctrl+Y)"), bar);
    saveButton_ = toolbarButton(QStringLiteral("save"), QStringLiteral("Save all (Ctrl+S)"), bar);
    revertButton_ = toolbarButton(QStringLiteral("revert"), QStringLiteral("Revert document to original"), bar);
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
    buildButton_ = new QPushButton(QStringLiteral("Build"), bar);
    buildButton_->setProperty("size", QStringLiteral("large"));
    buildButton_->setFont(theme::font(theme::TextRole::Body, 12.5));
    buildButton_->setToolTip(QStringLiteral("Build the ROM (Ctrl+B)"));
    connect(buildButton_, &QPushButton::clicked, this, [this] {
        if (isBusy()) {
            cancel();
        } else {
            buildAction_->trigger();
        }
    });
    actions->addWidget(buildButton_);
    actions->addSpacing(4);
    playButton_ = new QPushButton(QStringLiteral("Play"), bar);
    playButton_->setProperty("size", QStringLiteral("large"));
    playButton_->setFont(theme::font(theme::TextRole::Body, 12.5));
    playButton_->setIcon(theme::icon(QStringLiteral("play")));
    playButton_->setIconSize(QSize(11, 11));
    playButton_->setToolTip(QStringLiteral("Save, build and play from power-on (Ctrl+F5)"));
    connect(playButton_, &QPushButton::clicked, playAction_, &QAction::trigger);
    actions->addWidget(playButton_);
    actions->addSpacing(4);
    playSelectedButton_ = new ui::PrimaryButton(QStringLiteral("Play from selected"), QStringLiteral("play"), bar);
    connect(playSelectedButton_, &QAbstractButton::clicked, playSelectedAction_, &QAction::trigger);
    actions->addWidget(playSelectedButton_);
    // The running game's controls; live only while the emulator runs.
    actions->addSpacing(2);
    pauseButton_ = toolbarButton(QStringLiteral("pause"), QStringLiteral("Pause the playtest (Pause)"), bar);
    stopButton_ = toolbarButton(QStringLiteral("stop"), QStringLiteral("Stop the playtest (Shift+F5)"), bar);
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
    crumbEdited_ = new ui::Label(QStringLiteral("● edited"), ui::Label::Role::Meta, crumb);
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
    layout->addWidget(new ui::Label(QStringLiteral("Retrolarium Studio"), ui::Label::Role::Caps, column));
    auto* title = new ui::Label(QStringLiteral("Open a project to start"), ui::Label::Role::Title, column);
    layout->addWidget(title);
    auto* note = new ui::Label(
        QStringLiteral("Projects are .rlj files kept with a game's disassembly, in its "
                       "retrolarium folder. Open one with File → Open project or Ctrl+O."),
        ui::Label::Role::Small, column);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto* open = new ui::PrimaryButton(QStringLiteral("Open project…"), QStringLiteral("folder"), column);
    connect(open, &QAbstractButton::clicked, this, &MainWindow::chooseProject);
    buttons->addWidget(open);
    for (const auto& plugin : sdk::PluginRegistry::instance().plugins()) {
        for (const sdk::ProjectAction& action : plugin->projectActions()) {
            auto* button = new QPushButton(action.text, column);
            button->setProperty("size", QStringLiteral("large"));
            button->setFont(theme::font(theme::TextRole::Body, 12.5));
            const auto run = action.run;
            connect(button, &QPushButton::clicked, this, [this, run] { run(*this); });
            buttons->addWidget(button);
        }
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
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

// ---------------------------------------------------------------- projects

QWidget* MainWindow::dialogParent() const {
    return const_cast<MainWindow*>(this);
}

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

void MainWindow::log(const QString& line) {
    bottom_->appendLog(line);
}

void MainWindow::chooseProject() {
    if (!confirmDiscard()) {
        return;
    }
    const QString start = projectPath_.isEmpty() ? QString() : QFileInfo(projectPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open project"), start,
        QStringLiteral("Retrolarium projects (*.rlj);;All files (*)"));
    if (!path.isEmpty()) {
        openProjectFile(path);
    }
}

bool MainWindow::openProjectFile(const QString& path) {
    QString error;
    const sdk::ProjectFile project = sdk::ProjectFile::load(path, &error);
    if (!project.isValid()) {
        report(message(sdk::Severity::Error, QStringLiteral("project"), error, QFileInfo(path).fileName()));
        return false;
    }
    sdk::Plugin* plugin = sdk::PluginRegistry::instance().pluginFor(project);
    if (!plugin) {
        report(message(sdk::Severity::Error, QStringLiteral("project"),
                QStringLiteral("No plugin opens profile '%1' (%2).")
                    .arg(project.string(QStringLiteral("project.profile")),
                         project.string(QStringLiteral("project.platform"))),
                QFileInfo(path).fileName()));
        return false;
    }
    std::unique_ptr<sdk::Session> session = plugin->open(project, &error);
    if (!session) {
        report(message(sdk::Severity::Error, plugin->id(), error, QFileInfo(path).fileName()));
        return false;
    }

    closeProject();
    session_ = std::move(session);
    session_->attachHost(this);
    projectPath_ = project.path();
    rememberRecent(projectPath_);

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
    connect(session_.get(), &sdk::Session::selectedStartChanged, this, &MainWindow::updateActions);

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

void MainWindow::closeProject() {
    inspector_->showMessage(QString(), QString(), QString());
    if (processes_->isBusy()) {
        processes_->cancel();
    }
    emulator_->stopNow();
    audio_->stop();
    releaseEditor();
    session_.reset();
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
    updateProjectChrome();
    updateToolbarRow();
    updateDirtyState();
    updateStatus();
}

bool MainWindow::confirmDiscard() {
    if (!session_ || !session_->isModified()) {
        return true;
    }
    enum Answer { Cancel, Discard, Save };
    frame::MessageDialog box(QStringLiteral("Unsaved changes"),
                             QStringLiteral("Save changes to %1?").arg(session_->profile().name),
                             QStringLiteral("The project has edits that are not saved. Closing without "
                                            "saving loses them."),
                             this);
    box.addButton(QStringLiteral("Don't save"), Discard);
    box.addButton(QStringLiteral("Cancel"), Cancel);
    box.addButton(QStringLiteral("Save"), Save, true);
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
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscard()) {
        event->accept();
    } else {
        event->ignore();
    }
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
        QAction* item = recentMenu_->addAction(path);
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
            label = k.label;
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
        connect(editor_.get(), &editors::EditorView::actionFailed, this,
                [this](const QString& message) {
                    updateStatus(message);
                    toast_->showMessage(message);
                });
        connect(model_.get(), &sdk::EditorModel::contentChanged, this, [this] {
            updateBreadcrumb();
            updateStatus();
        });
        connect(model_.get(), &sdk::EditorModel::selectionChanged, this, [this] { updateStatus(); });
        inspector_->setModel(model_.get());
    } else {
        QString label = kind_;
        sdk::EditorType type = sdk::EditorType::None;
        for (const sdk::Kind& k : session_->kinds()) {
            if (k.id == kind_) {
                label = k.label;
                type = k.editor;
            }
        }
        emptyTitle_->setText(document_.isEmpty() ? QStringLiteral("Nothing to edit")
                                                 : QStringLiteral("%1 is coming next").arg(label));
        emptyNote_->setText(
            document_.isEmpty()
                ? QStringLiteral("This kind has no documents.")
                : QStringLiteral("The %1 for this kind is not ported to the new studio yet. "
                                 "Its documents are listed so the profile can be checked.")
                      .arg(sdk::editorName(type)));
        editorStack_->setCurrentWidget(emptyEditor_);
        inspector_->showMessage(label.toUpper(), document_.isEmpty() ? label : document_,
                                QStringLiteral("No inspector until the editor is ported."));
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
        toolbarContent_ = new QWidget(toolbarRow_);
        auto* row = new QHBoxLayout(toolbarContent_);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(12);
        row->addWidget(editor_->createToolbar(toolbarContent_), 1);
        // Where "Play from selected" starts, when this document sets it.
        const QString start = model_ ? model_->startPoint() : QString();
        if (!start.isEmpty()) {
            auto* chip = new ui::ViewChip(QStringLiteral("Start point"), start, toolbarContent_);
            chip->setToolTip(QStringLiteral("Play from selected (F5) starts here"));
            row->addWidget(chip);
        }
        for (const sdk::EditorAction& action : model_ ? model_->actions() : QVector<sdk::EditorAction>()) {
            auto* button = new ui::PrimaryButton(action.label, action.icon, toolbarContent_);
            button->setDanger(action.danger);
            button->setToolTip(action.tooltip);
            connect(button, &QAbstractButton::clicked, this, [this, action] {
                if (!action.confirmHeading.isEmpty() &&
                    !frame::MessageDialog::confirm(this, action.label, action.confirmHeading, action.confirmText,
                                                   action.confirmAction.isEmpty() ? action.label : action.confirmAction)) {
                    return;
                }
                // The model acts before the Playtest view replaces its editor.
                if (model_) model_->triggerAction(action.id);
                if (action.showsPlaytest) showPlaytest();
            });
            row->addWidget(button);
        }
        toolbarRowLayout_->addWidget(toolbarContent_);
    }
    toolbarRow_->setVisible(editor_ != nullptr);
}

// ---------------------------------------------------------------- state

void MainWindow::updateProjectChrome() {
    if (!session_) {
        platformBadge_->setText(QStringLiteral("—"));
        projectName_->setText(QStringLiteral("No project"));
        revisionBadge_->hide();
        setWindowTitle(QStringLiteral("Retrolarium"));
        titleBar()->setTitle(QStringLiteral("Retrolarium Studio"));
        projectButton_->setMinimumWidth(projectButton_->layout()->sizeHint().width());
        closeAction_->setEnabled(false);
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
                              : QStringLiteral("%1 (%2) [%3]").arg(profile.name, profile.revision,
                                                                   profile.platform);
    setWindowTitle(title + QStringLiteral(" — Retrolarium"));
    titleBar()->setTitle(title, QStringLiteral("Retrolarium"));
    closeAction_->setEnabled(true);
}

void MainWindow::updateBreadcrumb() {
    if (!session_) {
        return;
    }
    QString kindLabel = kind_;
    for (const sdk::Kind& k : session_->kinds()) {
        if (k.id == kind_) {
            kindLabel = k.label;
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
    QUndoStack* undo = open ? session_->undoStack() : nullptr;
    undoAction_->setEnabled(undo && undo->canUndo());
    redoAction_->setEnabled(undo && undo->canRedo());
    undoButton_->setEnabled(undoAction_->isEnabled());
    redoButton_->setEnabled(redoAction_->isEnabled());
    undoButton_->setToolTip(undo && undo->canUndo()
                                ? QStringLiteral("Undo %1 (Ctrl+Z)").arg(undo->undoText())
                                : QStringLiteral("Undo (Ctrl+Z)"));
    redoButton_->setToolTip(undo && undo->canRedo()
                                ? QStringLiteral("Redo %1 (Ctrl+Y)").arg(undo->redoText())
                                : QStringLiteral("Redo (Ctrl+Y)"));
    const bool modified = open && session_->isModified();
    saveAction_->setEnabled(modified);
    saveButton_->setEnabled(modified);
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
    dirtyLabel_->setText(dirty == 1 ? QStringLiteral("1 unsaved document")
                                    : QStringLiteral("%1 unsaved documents").arg(dirty));
    updateBreadcrumb();
}

void MainWindow::updateStatus(const QString& transient) {
    if (!session_) {
        statusLeft_->setText(QStringLiteral("No project open"));
        statusRight_->clear();
        return;
    }
    QString left = transient;
    if (left.isEmpty() && model_) {
        left = model_->statusText();
    }
    statusLeft_->setText(left);
    const sdk::Profile profile = session_->profile();
    statusRight_->setText(QStringLiteral("%1 · %2 · undo %3")
                              .arg(profile.platform, profile.cpu)
                              .arg(session_->undoStack()->index()));
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
        updateStatus(QStringLiteral("Saved"));
    } else {
        report(message(sdk::Severity::Error, QStringLiteral("save"), error, QString()));
    }
    updateDirtyState();
    documents_->updateGroups(session_->documents(kind_));
}

void MainWindow::revertDocument() {
    if (!session_ || !session_->canRevert(kind_, document_)) {
        return;
    }
    if (!frame::MessageDialog::confirm(
            this, QStringLiteral("Revert document"), QStringLiteral("Revert %1?").arg(crumbDocument_->text()),
            QStringLiteral("It returns to the state it had when the project was created. This can be undone."),
            QStringLiteral("Revert"))) {
        return;
    }
    QString error;
    if (!session_->revert(kind_, document_, &error)) {
        report(message(sdk::Severity::Error, QStringLiteral("revert"), error, crumbDocument_->text()));
    }
}

// ---------------------------------------------------------------- host services

void MainWindow::runProcess(const sdk::ProcessSpec& spec,
                            std::function<void(const sdk::ProcessResult&)> done) {
    processes_->run(spec, std::move(done));
}

bool MainWindow::isBusy() const {
    return processes_->isBusy();
}

void MainWindow::cancel() {
    processes_->cancel();
}

sdk::ProcessSpec MainWindow::kitCommand(const QStringList& arguments, const QString& workingDirectory,
                                        QString* error) const {
    return runtime::ToolLocator::kitCommand(arguments, workingDirectory, error);
}

QString MainWindow::toolPath(const QString& name) const {
    return runtime::ToolLocator::toolPath(name);
}

QString MainWindow::emulatorPath(const QString& id) const {
    return runtime::ToolLocator::emulatorPath(id);
}

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

void MainWindow::stopEmulator() {
    emulator_->stop();
}

void MainWindow::launchEmulator(const sdk::ProcessSpec& spec, const QString& title,
                                std::function<void(const sdk::ProcessResult&)> finished) {
    const QString kind = emulatorKind();
    if (kind.isEmpty()) {
        sdk::ProcessResult result;
        result.error = QStringLiteral("This project has no Playtest view.");
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

void MainWindow::playAudio(const QString& wavPath) {
    audio_->play(wavPath);
}

void MainWindow::stopAudio() {
    audio_->stop();
}

bool MainWindow::audioLoops() const {
    return audio_->isLooping();
}

void MainWindow::setAudioLoop(bool loop) {
    audio_->setLooping(loop);
}

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
    playSelectedAction_->setText(!label.isEmpty() ? label
                                 : start.isEmpty() ? QStringLiteral("Play from selected")
                                                   : QStringLiteral("Play from %1").arg(start));
    playSelectedButton_->setText(label.isEmpty() ? QStringLiteral("Play from selected") : label);
    buildButton_->setEnabled(canBuild || busy);
    buildButton_->setText(busy ? QStringLiteral("Cancel") : QStringLiteral("Build"));
    playButton_->setEnabled(canPlay && !busy);
    playSelectedButton_->setEnabled(!start.isEmpty() && !busy);
    playSelectedButton_->setToolTip(start.isEmpty()
                                        ? QStringLiteral("Pick a start point in an editor (F5)")
                                        : QStringLiteral("Save, build and play from %1 (F5)").arg(start));

    using Phase = runtime::EmulatorRunner::Phase;
    const Phase phase = emulator_->phase();
    const bool running = phase == Phase::Running;
    const bool live = running || phase == Phase::Starting;
    const bool paused = emulator_->isPaused();
    pauseGameAction_->setEnabled(running);
    pauseGameAction_->setText(paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    pauseButton_->setEnabled(running);
    pauseButton_->setIcon(theme::icon(paused ? QStringLiteral("play") : QStringLiteral("pause")));
    pauseButton_->setToolTip(paused ? QStringLiteral("Resume the playtest (Pause)")
                                    : QStringLiteral("Pause the playtest (Pause)"));
    stopGameAction_->setEnabled(live);
    stopButton_->setEnabled(live);
}

void MainWindow::togglePlaytestPause() {
    if (emulator_->phase() == runtime::EmulatorRunner::Phase::Running && !emulator_->togglePause()) {
        report(message(sdk::Severity::Warning, QStringLiteral("playtest"),
                       QStringLiteral("The emulator could not be paused."), QString()));
    }
}

}  // namespace rl::shell
