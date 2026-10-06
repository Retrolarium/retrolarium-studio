#include "setup_window.hpp"

#include "frame/title_bar.hpp"
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
#include <QLocale>
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
    dialog.addButton(text("OK", "ОК"), 0, true);
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

LanguageDialog::LanguageDialog() : StudioDialog(QStringLiteral("Retrolarium Studio")) {
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
    language_->addItem(QStringLiteral("English"), false);
    language_->addItem(QStringLiteral("Русский"), true);
    language_->setCurrentIndex(QLocale::system().language() == QLocale::Russian ? 1 : 0);
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

bool LanguageDialog::russianChosen() const {
    return language_->currentData().toBool();
}

void LanguageDialog::retranslate() {
    const bool ru = russianChosen();
    titleBar()->setTitle(ru ? QStringLiteral("Язык установки") : QStringLiteral("Setup language"));
    prompt_->setText(ru ? QStringLiteral("Выберите язык установки. Студия запустится на нём же.")
                        : QStringLiteral("Choose the language of setup. The studio starts in it too."));
    cancel_->setText(ru ? QStringLiteral("Отмена") : QStringLiteral("Cancel"));
    ok_->setText(ru ? QStringLiteral("Продолжить") : QStringLiteral("Continue"));
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
    const QString windowTitle = version.isEmpty() ? text("Retrolarium Studio setup", "Установка Retrolarium Studio")
                                                  : text("Retrolarium Studio %1 setup", "Установка Retrolarium Studio %1")
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
    back_ = plainButton(text("Back", "Назад"), body());
    next_ = new ui::PrimaryButton(QString(), QString(), body());
    cancel_ = plainButton(text("Cancel", "Отмена"), body());
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
    install_ = new QRadioButton(text("Install Retrolarium Studio", "Установить Retrolarium Studio"), page);
    repair_ = new QRadioButton(text("Repair the installed studio", "Восстановить установленную студию"), page);
    uninstall_ = new QRadioButton(text("Uninstall Retrolarium Studio", "Удалить Retrolarium Studio"), page);
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
        layout->addWidget(paragraph(text("Installed in %1. Installing again updates it; your games and edits stay.",
                                         "Установлена в %1. Повторная установка обновит её; ваши игры и правки "
                                         "сохранятся.")
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
    layout->addWidget(new ui::Label(text("Install to", "Папка установки"), ui::Label::Role::Caps, page));
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    path_ = new QLineEdit(nativePath(installDirectory_), page);
    path_->setFont(theme::font(theme::TextRole::Body, 13.0));
    row->addWidget(path_, 1);
    auto* browse = plainButton(text("Browse…", "Обзор…"), page);
    row->addWidget(browse);
    layout->addLayout(row);
    layout->addSpacing(14);
    desktop_ = new QCheckBox(text("Put a shortcut on the desktop", "Создать ярлык на рабочем столе"), page);
    desktop_->setFont(theme::font(theme::TextRole::Body, 13.0));
    desktop_->setChecked(installedDirectory_.isEmpty() || QFile::exists(desktopShortcutPath()));
    layout->addWidget(desktop_);
    layout->addSpacing(10);
    layout->addWidget(paragraph(text("The studio carries everything it needs: its kit, Python and MAME. It goes into "
                                     "your user folder, so setup needs no administrator rights, and .rlj files open "
                                     "in it.",
                                     "Студия несёт с собой всё, что ей нужно: кит, Python и MAME. Она ставится в "
                                     "папку пользователя, поэтому права администратора не нужны, а файлы .rlj будут "
                                     "открываться в ней."),
                                page));
    layout->addStretch(1);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, text("Choose the installation folder", "Выберите папку установки"), path_->text());
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
    removeUserData_ = new QCheckBox(text("Also delete your games, edits and the studio's settings",
                                         "Удалить также ваши игры, правки и настройки студии"),
                                    page);
    removeUserData_->setFont(theme::font(theme::TextRole::Body, 13.0));
    layout->addWidget(removeUserData_);
    removeUserDataNote_ = paragraph(text("The games the studio downloaded and their edits are in<br>%1",
                                         "Скачанные студией игры и правки в них лежат в<br>%1")
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
    launch_ = new QCheckBox(text("Start Retrolarium Studio", "Запустить Retrolarium Studio"), page);
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
        title_->setText(uninstallOnly_ ? text("Uninstall Retrolarium Studio", "Удаление Retrolarium Studio")
                                       : text("Welcome to Retrolarium Studio", "Добро пожаловать в Retrolarium Studio"));
        subtitle_->setText(text("Choose what setup should do.", "Выберите, что сделать."));
        next_->setText(text("Next", "Далее"));
        break;
    case Options:
        title_->setText(text("Where to install", "Куда установить"));
        subtitle_->setText(text("A new folder, or the one of an earlier installation.",
                                "Новая папка или папка прежней установки."));
        next_->setText(text("Next", "Далее"));
        break;
    case Ready:
        prepareReady();
        break;
    case Running:
        title_->setText(mode_ == Mode::Uninstall ? text("Uninstalling", "Удаление")
                                                 : text("Installing", "Установка"));
        subtitle_->setText(text("This takes a minute.", "Это займёт около минуты."));
        next_->setText(text("Next", "Далее"));
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
        tell(this, windowTitle(), text("Choose another folder", "Выберите другую папку"),
             text("The studio needs a full folder path that is not the root of a drive.",
                  "Нужен полный путь к папке, и это не может быть корень диска."));
        return false;
    }
    if (!replaceableDirectory(chosen)) {
        tell(this, windowTitle(), text("Choose another folder", "Выберите другую папку"),
             text("%1 already holds other files. Choose an empty or new folder, for example %2.",
                  "В папке %1 уже есть другие файлы. Выберите пустую или новую папку, например %2.")
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
        title_->setText(text("Ready to install", "Всё готово к установке"));
        subtitle_->setText(text("An earlier version in that folder is replaced; what you added to it stays.",
                                "Прежняя версия в этой папке будет заменена; то, что вы в неё добавили, "
                                "сохранится."));
        summary_->setText(text("Retrolarium Studio %1 goes into<br>%2<br><br>Shortcuts go to the Start menu%3.",
                               "Retrolarium Studio %1 будет установлена в<br>%2<br><br>Ярлыки появятся в меню "
                               "«Пуск»%3.")
                              .arg(manifest_.version, emphasis(nativePath(installDirectory_)),
                                   desktop_->isChecked() ? text(" and on the desktop", " и на рабочем столе") : QString()));
        next_->setText(text("Install", "Установить"));
    } else if (mode_ == Mode::Repair) {
        title_->setText(text("Ready to repair", "Всё готово к восстановлению"));
        subtitle_->setText(text("The studio's files and shortcuts are put back; your games and settings stay.",
                                "Файлы и ярлыки студии будут восстановлены; ваши игры и настройки сохранятся."));
        summary_->setText(text("Retrolarium Studio %1 is reinstalled in<br>%2",
                               "Retrolarium Studio %1 будет заново установлена в<br>%2")
                              .arg(manifest_.version, emphasis(nativePath(installedDirectory_))));
        next_->setText(text("Repair", "Восстановить"));
    } else {
        title_->setText(text("Ready to uninstall", "Всё готово к удалению"));
        subtitle_->setText(text("Your games and edits stay unless you ask otherwise.",
                                "Ваши игры и правки сохранятся, если не выбрать иное."));
        summary_->setText(text("Retrolarium Studio is removed from<br>%1<br><br>with its shortcuts, its entry in "
                               "Installed Apps and the .rlj file association.",
                               "Retrolarium Studio будет удалена из<br>%1<br><br>вместе с ярлыками, записью в "
                               "списке программ и связью с файлами .rlj.")
                              .arg(emphasis(nativePath(installedDirectory_))));
        next_->setText(text("Uninstall", "Удалить"));
    }
}

void SetupWindow::startOperation() {
    const QString directory = mode_ == Mode::Install ? installDirectory_ : installedDirectory_;
    for (;;) {
        const QStringList running = runningPrograms(directory);
        if (running.isEmpty()) break;
        if (!ask(this, windowTitle(), text("Close Retrolarium Studio first", "Сначала закройте Retrolarium Studio"),
                 text("These programs still run from the studio's folder: %1. Close them, then try again.",
                      "Из папки студии ещё запущены: %1. Закройте их и повторите.")
                     .arg(running.join(QStringLiteral(", "))),
                 text("Try again", "Повторить"), text("Cancel", "Отмена"))) {
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
        !ask(this, windowTitle(), text("Delete your games and edits?", "Удалить ваши игры и правки?"),
             text("Everything in %1 is deleted: the games the studio downloaded and every edit in them. This cannot be "
                  "undone.",
                  "Будет удалено всё в %1: скачанные студией игры и все правки в них. Отменить это нельзя.")
                 .arg(nativePath(gamesDirectory())),
             text("Delete", "Удалить"), text("Keep them", "Оставить"))) {
        return;
    }
    running_ = true;
    succeeded_ = false;
    error_.clear();
    warning_.clear();
    setProgress(0, text("Preparing…", "Подготовка…"));
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
            setProgress(100, text("Done.", "Готово."));
            QTimer::singleShot(300, this, [this] { showPage(Finished); });
        } else {
            showPage(Ready);
            tell(this, windowTitle(), text("Setup did not finish", "Установка не завершилась"), error_);
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
    title_->setText(installed ? text("Retrolarium Studio is ready", "Retrolarium Studio готова")
                              : text("Retrolarium Studio is removed", "Retrolarium Studio удалена"));
    subtitle_->setText(installed ? text("Setup finished.", "Установка завершена.")
                                 : text("Uninstall finished.", "Удаление завершено."));
    QString message = installed
                          ? text("Open your ROM in the studio and it finds the game, downloads its files the first time "
                                 "and opens the version you have.",
                                 "Откройте в студии свой ROM: она определит игру, при первом открытии скачает её файлы "
                                 "и откроет вашу версию.")
                          : (removeUserData_->isChecked()
                                 ? text("The studio, its shortcuts, your games and its settings were removed.",
                                        "Студия, её ярлыки, ваши игры и настройки удалены.")
                                 : text("The studio and its shortcuts were removed. Your games and edits stay in<br>%1",
                                        "Студия и её ярлыки удалены. Ваши игры и правки остались в<br>%1")
                                       .arg(emphasis(nativePath(gamesDirectory()))));
    if (!warning_.isEmpty()) message += QStringLiteral("<br><br>%1").arg(warning_.toHtmlEscaped());
    finishedMessage_->setText(message);
    launch_->setVisible(installed);
    next_->setText(text("Finish", "Готово"));
}

void SetupWindow::finish() {
    if (mode_ != Mode::Uninstall && launch_->isChecked()) {
        QString error;
        if (!launchStudio(mode_ == Mode::Install ? installDirectory_ : installedDirectory_, &error)) {
            tell(this, windowTitle(), text("The studio did not start", "Студия не запустилась"), error);
        }
    }
    if (mode_ == Mode::Uninstall) {
        const QString self = QCoreApplication::applicationFilePath();
        const QString uninstaller = QDir(installedDirectory_).filePath(kUninstallerName);
        QString error;
        if (QFile::exists(uninstaller) && sameFile(self, uninstaller) &&
            !scheduleSelfRemoval(self, installedDirectory_, &error)) {
            tell(this, windowTitle(), text("Could not finish uninstalling", "Не удалось завершить удаление"), error);
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
    if (ask(this, windowTitle(), text("Exit setup?", "Выйти из установки?"),
            text("Nothing has been changed yet.", "Пока ничего не изменено."), text("Exit", "Выйти"),
            text("Stay", "Остаться"))) {
        QDialog::reject();
    }
}

}  // namespace rl::setup
