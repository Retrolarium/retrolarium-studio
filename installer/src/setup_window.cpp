#include "setup_window.hpp"

#include "frame/title_bar.hpp"
#include "sdk/i18n.hpp"
#include "sdk/languages.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/brand_mark.hpp"
#include "widgets/budget_bar.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>
#include <tuple>

namespace rl::setup {

namespace {

constexpr int kMargin = 28;

QPushButton* plainButton(const QString& label, QWidget* parent) {
    auto* button = new QPushButton(label, parent);
    button->setAutoDefault(false);
    button->setFont(theme::font(theme::TextRole::Body, 13.0));
    button->setMinimumWidth(92);
    return button;
}

ui::Label* paragraph(const QString& value, QWidget* parent, const QString& tone = QStringLiteral("muted")) {
    auto* label = new ui::Label(value, ui::Label::Role::Body, parent);
    label->setTone(tone);
    label->setWordWrap(true);
    return label;
}

// A question in the studio's message frame; true when `action` is taken.
bool ask(QWidget* parent, const QString& title, const QString& heading, const QString& body, const QString& action,
         const QString& stay) {
    frame::MessageDialog dialog(title, heading, body, parent);
    dialog.addButton(stay, 0);
    dialog.addButton(action, 1, true);
    dialog.setCancelResult(0);
    return dialog.exec() == 1;
}

void tell(QWidget* parent, const QString& title, const QString& heading, const QString& body) {
    frame::MessageDialog dialog(title, heading, body, parent);
    dialog.addButton(rlTr("setup", "OK"), 0, true);
    dialog.exec();
}

QString nativePath(const QString& path) {
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

// A value inside muted rich text, in the text colour.
QString emphasis(const QString& value) {
    return QStringLiteral("<span style=\"color: %1; white-space: nowrap\">%2</span>")
        .arg(theme::current().text.name(), value.toHtmlEscaped());
}

}  // namespace

// ---------------------------------------------------------------- language

LanguageDialog::LanguageDialog(std::function<void(const QString&)> use)
    : StudioDialog(QStringLiteral("Retrolarium Studio")), use_(std::move(use)) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(10);
    auto* heading = new QHBoxLayout;
    heading->setSpacing(14);
    heading->addWidget(new ui::BrandMark(32, body()), 0, Qt::AlignVCenter);
    heading->addWidget(new ui::Label(QStringLiteral("Retrolarium Studio"), ui::Label::Role::Title, body()), 1);
    content->addLayout(heading);
    prompt_ = paragraph(QString(), body());
    prompt_->setFixedWidth(340);
    content->addWidget(prompt_);
    language_ = new QComboBox(body());
    language_->setFont(theme::font(theme::TextRole::Body, 13.0));
    const QString system = systemInterfaceLanguage();
    for (const InterfaceLanguage& language : interfaceLanguages()) {
        language_->addItem(language.name, language.code);
        if (language.code == system) language_->setCurrentIndex(language_->count() - 1);
    }
    language_->setFixedWidth(220);
    content->addWidget(language_);
    content->addSpacing(10);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch(1);
    cancel_ = plainButton(QString(), body());
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel_);
    ok_ = new ui::PrimaryButton(QString(), QString(), body());
    connect(ok_, &QAbstractButton::clicked, this, &QDialog::accept);
    buttons->addWidget(ok_);
    content->addLayout(buttons);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, ok_, &QAbstractButton::click);
    }
    connect(language_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { retranslate(); });
    retranslate();
}

QString LanguageDialog::chosen() const {
    return language_->currentData().toString();
}

void LanguageDialog::retranslate() {
    use_(chosen());
    titleBar()->setTitle(rlTr("setup", "Setup language"));
    prompt_->setText(rlTr("setup", "Choose the language of setup. The studio starts in it too."));
    cancel_->setText(rlTr("setup", "Cancel"));
    ok_->setText(rlTr("setup", "Continue"));
    ok_->updateGeometry();
}

// ---------------------------------------------------------------- wizard

SetupWindow::SetupWindow(const QString& packagePath, bool uninstallOnly)
    : StudioDialog(QString()), packagePath_(packagePath), installedDirectory_(installedDirectory()),
      uninstallOnly_(uninstallOnly) {
    InstallerPackage::read(packagePath, &manifest_, &packageError_);
    // The installed Uninstall.exe still finds its installation when the
    // Installed Apps entry is gone.
    if (uninstallOnly_ && installedDirectory_.isEmpty() &&
        QFileInfo(packagePath).fileName().compare(kUninstallerName, Qt::CaseInsensitive) == 0) {
        installedDirectory_ = QFileInfo(packagePath).absolutePath();
    }
    installDirectory_ = installedDirectory_.isEmpty() ? defaultInstallDirectory() : installedDirectory_;
    if (uninstallOnly_) mode_ = Mode::Uninstall;
    QString version = manifest_.version.isEmpty() ? installedVersion() : manifest_.version;
    const QString windowTitle = version.isEmpty() ? rlTr("setup", "Retrolarium Studio setup")
                                                  : rlTr("setup", "Retrolarium Studio %1 setup")
                                                        .arg(version);
    setWindowTitle(windowTitle);
    titleBar()->setTitle(windowTitle);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    setFixedSize(640, 470);

    auto* layout = new QVBoxLayout(body());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The page's heading beside the mark.
    auto* header = new QWidget(body());
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kMargin, 22, kMargin, 16);
    headerLayout->setSpacing(18);
    headerLayout->addWidget(new ui::BrandMark(48, header), 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(4);
    title_ = new ui::Label(QString(), ui::Label::Role::Title, header);
    subtitle_ = paragraph(QString(), header);
    titles->addWidget(title_);
    titles->addWidget(subtitle_);
    headerLayout->addLayout(titles, 1);
    layout->addWidget(header);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, body()));

    // The buttons first: the pages refer to them.
    back_ = plainButton(rlTr("setup", "Back"), body());
    next_ = new ui::PrimaryButton(QString(), QString(), body());
    cancel_ = plainButton(rlTr("setup", "Cancel"), body());
    pages_ = new QStackedWidget(body());
    pages_->addWidget(welcomePage());
    pages_->addWidget(optionsPage());
    pages_->addWidget(readyPage());
    pages_->addWidget(runningPage());
    pages_->addWidget(finishedPage());
    layout->addWidget(pages_, 1);

    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, body()));
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(kMargin, 12, 20, 14);
    footer->setSpacing(8);
    footer->addStretch(1);
    connect(back_, &QPushButton::clicked, this, &SetupWindow::back);
    footer->addWidget(back_);
    connect(next_, &QAbstractButton::clicked, this, &SetupWindow::next);
    footer->addWidget(next_);
    connect(cancel_, &QPushButton::clicked, this, &SetupWindow::reject);
    footer->addWidget(cancel_);
    layout->addLayout(footer);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, next_, &QAbstractButton::click);
    }
    showPage(Welcome);
}

QWidget* SetupWindow::welcomePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin + 66, 22, kMargin, 16);
    layout->setSpacing(12);
    install_ = new QRadioButton(rlTr("setup", "Install Retrolarium Studio"), page);
    repair_ = new QRadioButton(rlTr("setup", "Repair the installed studio"), page);
    uninstall_ = new QRadioButton(rlTr("setup", "Uninstall Retrolarium Studio"), page);
    const bool installed = !installedDirectory_.isEmpty();
    const bool package = packageError_.isEmpty();
    install_->setEnabled(package);
    repair_->setEnabled(installed && package);
    uninstall_->setEnabled(installed);
    for (QRadioButton* button : {install_, repair_, uninstall_}) {
        button->setFont(theme::font(theme::TextRole::Body, 13.5));
        layout->addWidget(button);
    }
    if (uninstallOnly_) {
        uninstall_->setChecked(true);
        install_->hide();
        repair_->hide();
    } else if (package) {
        install_->setChecked(true);
    } else if (installed) {
        uninstall_->setChecked(true);
    }
    if (installed && !uninstallOnly_) {
        layout->addSpacing(6);
        layout->addWidget(paragraph(rlTr("setup", "Installed in %1. Installing again updates it; your games and edits stay.")
                                        .arg(nativePath(installedDirectory_)),
                                    page));
    }
    if (!package && !uninstallOnly_) {
        layout->addSpacing(6);
        layout->addWidget(paragraph(packageError_, page, QStringLiteral("accent")));
    }
    layout->addStretch(1);
    return page;
}

QWidget* SetupWindow::optionsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin + 66, 22, kMargin, 16);
    layout->setSpacing(8);
    layout->addWidget(new ui::Label(rlTr("setup", "Install to"), ui::Label::Role::Caps, page));
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    path_ = new QLineEdit(nativePath(installDirectory_), page);
    path_->setFont(theme::font(theme::TextRole::Body, 13.0));
    row->addWidget(path_, 1);
    auto* browse = plainButton(rlTr("setup", "Browse…"), page);
    row->addWidget(browse);
    layout->addLayout(row);
    layout->addSpacing(14);
    desktop_ = new QCheckBox(rlTr("setup", "Put a shortcut on the desktop"), page);
    desktop_->setFont(theme::font(theme::TextRole::Body, 13.0));
    desktop_->setChecked(installedDirectory_.isEmpty() || QFile::exists(desktopShortcutPath()));
    layout->addWidget(desktop_);
    layout->addSpacing(10);
    layout->addWidget(paragraph(rlTr("setup", "The studio carries everything it needs: its kit, Python and MAME. It goes into "
                                     "your user folder, so setup needs no administrator rights, and .rlj files open "
                                     "in it."),
                                page));
    layout->addStretch(1);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, rlTr("setup", "Choose the installation folder"), path_->text());
        if (!chosen.isEmpty()) path_->setText(nativePath(chosen));
    });
    return page;
}

QWidget* SetupWindow::readyPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin + 66, 22, kMargin, 16);
    layout->setSpacing(12);
    summary_ = paragraph(QString(), page, QString());
    summary_->setTextFormat(Qt::RichText);
    layout->addWidget(summary_);
    layout->addSpacing(4);
    // Off by default and never remembered: the one choice here that deletes
    // something the user cannot get back.
    removeUserData_ = new QCheckBox(rlTr("setup", "Also delete your games, edits and the studio's settings"),
                                    page);
    removeUserData_->setFont(theme::font(theme::TextRole::Body, 13.0));
    layout->addWidget(removeUserData_);
    removeUserDataNote_ = paragraph(rlTr("setup", "The games the studio downloaded and their edits are in<br>%1")
                                        .arg(emphasis(nativePath(gamesDirectory()))),
                                    page);
    removeUserDataNote_->setTextFormat(Qt::RichText);
    removeUserDataNote_->setContentsMargins(20, 0, 0, 0);
    layout->addWidget(removeUserDataNote_);
    layout->addStretch(1);
    return page;
}

QWidget* SetupWindow::runningPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin + 66, 28, kMargin, 16);
    layout->setSpacing(10);
    runningMessage_ = paragraph(QString(), page, QString());
    layout->addWidget(runningMessage_);
    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    progress_ = new ui::BudgetBar(page);
    progress_->setBarThickness(6);
    // A budget bar is 80 pixels wide; the progress spans the page.
    progress_->setMinimumWidth(0);
    progress_->setMaximumWidth(QWIDGETSIZE_MAX);
    progress_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->addWidget(progress_, 1);
    percent_ = new ui::Label(QString(), ui::Label::Role::Mono, page);
    percent_->setTone(QStringLiteral("muted"));
    percent_->setMinimumWidth(40);
    row->addWidget(percent_);
    layout->addLayout(row);
    layout->addStretch(1);
    return page;
}

QWidget* SetupWindow::finishedPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin + 66, 22, kMargin, 16);
    layout->setSpacing(14);
    finishedMessage_ = paragraph(QString(), page);
    finishedMessage_->setTextFormat(Qt::RichText);
    layout->addWidget(finishedMessage_);
    launch_ = new QCheckBox(rlTr("setup", "Start Retrolarium Studio"), page);
    launch_->setFont(theme::font(theme::TextRole::Body, 13.0));
    launch_->setChecked(true);
    layout->addWidget(launch_);
    layout->addStretch(1);
    return page;
}

void SetupWindow::showPage(Page page) {
    page_ = page;
    pages_->setCurrentIndex(page);
    back_->setVisible(page == Options || page == Ready);
    cancel_->setVisible(page != Finished);
    cancel_->setEnabled(page != Running);
    next_->setVisible(page != Running);
    switch (page) {
    case Welcome:
        title_->setText(uninstallOnly_ ? rlTr("setup", "Uninstall Retrolarium Studio")
                                       : rlTr("setup", "Welcome to Retrolarium Studio"));
        subtitle_->setText(rlTr("setup", "Choose what setup should do."));
        next_->setText(rlTr("setup", "Next"));
        break;
    case Options:
        title_->setText(rlTr("setup", "Where to install"));
        subtitle_->setText(rlTr("setup", "A new folder, or the one of an earlier installation."));
        next_->setText(rlTr("setup", "Next"));
        break;
    case Ready:
        prepareReady();
        break;
    case Running:
        title_->setText(mode_ == Mode::Uninstall ? rlTr("setup", "Uninstalling")
                                                 : rlTr("setup", "Installing"));
        subtitle_->setText(rlTr("setup", "This takes a minute."));
        next_->setText(rlTr("setup", "Next"));
        break;
    case Finished:
        prepareFinished();
        break;
    }
    next_->updateGeometry();
}

void SetupWindow::next() {
    switch (page_) {
    case Welcome:
        if (!(install_->isChecked() || repair_->isChecked() || uninstall_->isChecked())) return;
        mode_ = uninstall_->isChecked() ? Mode::Uninstall : repair_->isChecked() ? Mode::Repair : Mode::Install;
        showPage(mode_ == Mode::Install ? Options : Ready);
        break;
    case Options:
        if (validateOptions()) showPage(Ready);
        break;
    case Ready:
        startOperation();
        break;
    case Running:
        break;
    case Finished:
        finish();
        break;
    }
}

void SetupWindow::back() {
    if (page_ == Options) showPage(Welcome);
    if (page_ == Ready) showPage(mode_ == Mode::Install ? Options : Welcome);
}

bool SetupWindow::validateOptions() {
    const QString chosen = QDir::fromNativeSeparators(path_->text().trimmed());
    if (chosen.isEmpty() || !QDir::isAbsolutePath(chosen) || QDir(chosen).isRoot()) {
        tell(this, windowTitle(), rlTr("setup", "Choose another folder"),
             rlTr("setup", "The studio needs a full folder path that is not the root of a drive."));
        return false;
    }
    if (!replaceableDirectory(chosen)) {
        tell(this, windowTitle(), rlTr("setup", "Choose another folder"),
             rlTr("setup", "%1 already holds other files. Choose an empty or new folder, for example %2.")
                 .arg(nativePath(chosen), nativePath(QDir(chosen).filePath(QStringLiteral("Retrolarium Studio")))));
        return false;
    }
    installDirectory_ = chosen;
    return true;
}

void SetupWindow::prepareReady() {
    removeUserData_->setVisible(mode_ == Mode::Uninstall);
    removeUserDataNote_->setVisible(mode_ == Mode::Uninstall);
    removeUserData_->setChecked(false);
    if (mode_ == Mode::Install) {
        title_->setText(rlTr("setup", "Ready to install"));
        subtitle_->setText(rlTr("setup", "An earlier version in that folder is replaced; what you added to it stays."));
        summary_->setText(rlTr("setup", "Retrolarium Studio %1 goes into<br>%2<br><br>Shortcuts go to the Start menu%3.")
                              .arg(manifest_.version, emphasis(nativePath(installDirectory_)),
                                   desktop_->isChecked() ? rlTr("setup", " and on the desktop") : QString()));
        next_->setText(rlTr("setup", "Install"));
    } else if (mode_ == Mode::Repair) {
        title_->setText(rlTr("setup", "Ready to repair"));
        subtitle_->setText(rlTr("setup", "The studio's files and shortcuts are put back; your games and settings stay."));
        summary_->setText(rlTr("setup", "Retrolarium Studio %1 is reinstalled in<br>%2")
                              .arg(manifest_.version, emphasis(nativePath(installedDirectory_))));
        next_->setText(rlTr("setup", "Repair"));
    } else {
        title_->setText(rlTr("setup", "Ready to uninstall"));
        subtitle_->setText(rlTr("setup", "Your games and edits stay unless you ask otherwise."));
        summary_->setText(rlTr("setup", "Retrolarium Studio is removed from<br>%1<br><br>with its shortcuts, its entry in "
                               "Installed Apps and the .rlj file association.")
                              .arg(emphasis(nativePath(installedDirectory_))));
        next_->setText(rlTr("setup", "Uninstall"));
    }
}

void SetupWindow::startOperation() {
    const QString directory = mode_ == Mode::Install ? installDirectory_ : installedDirectory_;
    for (;;) {
        const QStringList running = runningPrograms(directory);
        if (running.isEmpty()) break;
        if (!ask(this, windowTitle(), rlTr("setup", "Close Retrolarium Studio first"),
                 rlTr("setup", "These programs still run from the studio's folder: %1. Close them, then try again.")
                     .arg(running.join(QStringLiteral(", "))),
                 rlTr("setup", "Try again"), rlTr("setup", "Cancel"))) {
            return;
        }
    }
    Request request;
    request.mode = mode_;
    request.packagePath = packagePath_;
    request.manifest = manifest_;
    request.installDirectory = directory;
    request.desktopShortcut = desktop_->isChecked();
    request.removeUserData = mode_ == Mode::Uninstall && removeUserData_->isChecked();
    if (request.removeUserData &&
        !ask(this, windowTitle(), rlTr("setup", "Delete your games and edits?"),
             rlTr("setup", "Everything in %1 is deleted: the games the studio downloaded and every edit in them. This cannot be "
                  "undone.")
                 .arg(nativePath(gamesDirectory())),
             rlTr("setup", "Delete"), rlTr("setup", "Keep them"))) {
        return;
    }
    running_ = true;
    succeeded_ = false;
    error_.clear();
    warning_.clear();
    setProgress(0, rlTr("setup", "Preparing…"));
    showPage(Running);

    QPointer<SetupWindow> self(this);
    const Progress progress = [self](int percent, const QString& message) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, percent, message] {
                if (self) self->setProgress(percent, message);
            },
            Qt::QueuedConnection);
    };
    auto result = std::make_shared<std::tuple<bool, QString, QString>>();
    QThread* worker = QThread::create([request, progress, result] {
        QString error;
        QString warning;
        const bool ok = request.mode == Mode::Uninstall ? uninstall(request, progress, &error, &warning)
                                                        : install(request, progress, &error);
        *result = std::make_tuple(ok, error, warning);
    });
    connect(worker, &QThread::finished, this, [this, worker, result] {
        worker->deleteLater();
        running_ = false;
        std::tie(succeeded_, error_, warning_) = *result;
        if (succeeded_) {
            setProgress(100, rlTr("setup", "Done."));
            QTimer::singleShot(300, this, [this] { showPage(Finished); });
        } else {
            showPage(Ready);
            tell(this, windowTitle(), rlTr("setup", "Setup did not finish"), error_);
        }
    });
    worker->start();
}

void SetupWindow::setProgress(int percent, const QString& message) {
    progress_->setBudget(percent, 100);
    percent_->setText(QStringLiteral("%1%").arg(percent));
    runningMessage_->setTone(QString());
    runningMessage_->setText(message);
}

void SetupWindow::prepareFinished() {
    const bool installed = mode_ != Mode::Uninstall;
    title_->setText(installed ? rlTr("setup", "Retrolarium Studio is ready")
                              : rlTr("setup", "Retrolarium Studio is removed"));
    subtitle_->setText(installed ? rlTr("setup", "Setup finished.")
                                 : rlTr("setup", "Uninstall finished."));
    QString message = installed
                          ? rlTr("setup", "Open your ROM in the studio and it finds the game, downloads its files the first time "
                                 "and opens the version you have.")
                          : (removeUserData_->isChecked()
                                 ? rlTr("setup", "The studio, its shortcuts, your games and its settings were removed.")
                                 : rlTr("setup", "The studio and its shortcuts were removed. Your games and edits stay in<br>%1")
                                       .arg(emphasis(nativePath(gamesDirectory()))));
    if (!warning_.isEmpty()) message += QStringLiteral("<br><br>%1").arg(warning_.toHtmlEscaped());
    finishedMessage_->setText(message);
    launch_->setVisible(installed);
    next_->setText(rlTr("setup", "Finish"));
}

void SetupWindow::finish() {
    if (mode_ != Mode::Uninstall && launch_->isChecked()) {
        QString error;
        if (!launchStudio(mode_ == Mode::Install ? installDirectory_ : installedDirectory_, &error)) {
            tell(this, windowTitle(), rlTr("setup", "The studio did not start"), error);
        }
    }
    if (mode_ == Mode::Uninstall) {
        const QString self = QCoreApplication::applicationFilePath();
        const QString uninstaller = QDir(installedDirectory_).filePath(kUninstallerName);
        QString error;
        if (QFile::exists(uninstaller) && sameFile(self, uninstaller) &&
            !scheduleSelfRemoval(self, installedDirectory_, &error)) {
            tell(this, windowTitle(), rlTr("setup", "Could not finish uninstalling"), error);
        }
    }
    QDialog::accept();
}

void SetupWindow::reject() {
    if (running_) return;
    if (page_ == Finished) {
        finish();
        return;
    }
    if (ask(this, windowTitle(), rlTr("setup", "Exit setup?"),
            rlTr("setup", "Nothing has been changed yet."), rlTr("setup", "Exit"),
            rlTr("setup", "Stay"))) {
        QDialog::reject();
    }
}

}  // namespace rl::setup
