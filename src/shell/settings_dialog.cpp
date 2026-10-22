#include "shell/settings_dialog.hpp"

#include "runtime/tool_locator.hpp"
#include "sdk/languages.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace rl::shell {

namespace {

QPushButton* button(const QString& text, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    b->setFont(theme::font(theme::TextRole::Body, 13.0));
    b->setMinimumWidth(84);
    return b;
}

ui::Label* hint(const QString& text, QWidget* parent) {
    auto* label = new ui::Label(text, ui::Label::Role::Small, parent);
    label->setTone(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

// Where MAME finds the user's Disk System BIOS: roms/fds beside it, under
// the one name its fds machine loads (the kit installs either dump there), or
// a set archive. The kit's fds_bios looks in the same places.
QString biosPath(const QString& mame) {
    if (mame.isEmpty()) return {};
    const QDir roms(QFileInfo(mame).absoluteDir().filePath(QStringLiteral("roms")));
    for (const QString& name :
         {QStringLiteral("fds/rp2c33a-01a.bin"), QStringLiteral("fds.zip"), QStringLiteral("fds.7z")}) {
        if (QFileInfo(roms.filePath(name)).isFile()) return roms.filePath(name);
    }
    return {};
}

}  // namespace

QString SettingsDialog::language() {
    const QString chosen = QSettings().value(QString::fromLatin1(kLanguageKey)).toString();
    return isInterfaceLanguage(chosen) ? chosen : systemInterfaceLanguage();
}

SettingsDialog::SettingsDialog(QWidget* parent) : StudioDialog(tr("Settings"), parent) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(24, 20, 20, 18);
    content->setSpacing(8);
    content->addWidget(new ui::Label(tr("Settings"), ui::Label::Role::Title, body()));

    content->addSpacing(6);
    content->addWidget(new ui::Label(tr("Language"), ui::Label::Role::Caps, body()));
    language_ = new QComboBox(body());
    language_->setFont(theme::font(theme::TextRole::Body, 13.0));
    const QString current = language();
    for (const InterfaceLanguage& language : interfaceLanguages()) {
        language_->addItem(language.name, language.code);
        if (language.code == current) language_->setCurrentIndex(language_->count() - 1);
    }
    language_->setFixedWidth(200);
    // The interface is built in its language once: a new one shows after a
    // restart, which the button does at once, reopening the project.
    auto* languageRow = new QHBoxLayout;
    languageRow->setSpacing(12);
    languageRow->addWidget(language_);
    auto* restartNow = button(tr("Restart now"), body());
    restartNow->setVisible(false);
    connect(restartNow, &QPushButton::clicked, this, [this] {
        emit restartRequested();
        accept();
    });
    languageRow->addWidget(restartNow);
    languageRow->addStretch(1);
    content->addLayout(languageRow);
    auto* restart = hint(tr("The studio shows the new language the next time it starts."), body());
    restart->setVisible(false);
    content->addWidget(restart);
    connect(language_, QOverload<int>::of(&QComboBox::activated), this,
            [this, restart, restartNow, current](int index) {
                const QString code = language_->itemData(index).toString();
                QSettings().setValue(QString::fromLatin1(kLanguageKey), code);
                restart->setVisible(code != current);
                restartNow->setVisible(code != current);
            });

    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("Emulator"), ui::Label::Role::Caps, body()));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->addWidget(new ui::Label(QStringLiteral("MAME"), ui::Label::Role::Body, body()), 0, 0);
    mame_ = new ui::Label(QString(), ui::Label::Role::Mono, body());
    mame_->setFixedWidth(340);
    grid->addWidget(mame_, 0, 1);
    auto* chooseMame = button(tr("Choose…"), body());
    connect(chooseMame, &QPushButton::clicked, this, &SettingsDialog::chooseMame);
    grid->addWidget(chooseMame, 0, 2);
    grid->addWidget(new ui::Label(tr("Disk System BIOS"), ui::Label::Role::Body, body()), 1, 0);
    bios_ = new ui::Label(QString(), ui::Label::Role::Mono, body());
    bios_->setFixedWidth(340);
    grid->addWidget(bios_, 1, 1);
    auto* chooseBios = button(tr("Choose…"), body());
    connect(chooseBios, &QPushButton::clicked, this, &SettingsDialog::chooseBios);
    grid->addWidget(chooseBios, 1, 2);
    content->addLayout(grid);
    auto* biosNote = hint(tr("Famicom Disk System games (Super Mario Bros. 2, the Disk System Super "
                             "Mario Bros.) play on OpenDiskSysROM, a free BIOS that comes with the "
                             "studio. If a game misbehaves on it, choose your dump of the console's "
                             "BIOS, usually called disksys.rom: disk games then play on that."),
                          body());
    biosNote->setFixedWidth(560);
    content->addWidget(biosNote);

    content->addSpacing(10);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* close = new ui::PrimaryButton(tr("Close"), QString(), body());
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    content->addLayout(buttons);
    refresh();
}

void SettingsDialog::refresh() {
    const QString mame = runtime::ToolLocator::emulatorPath(QStringLiteral("mame"));
    mame_->setText(mame.isEmpty() ? tr("Not found")
                                  : mame_->fontMetrics().elidedText(QDir::toNativeSeparators(mame), Qt::ElideMiddle,
                                                                    mame_->width()));
    mame_->setToolTip(mame);
    const QString bios = biosPath(mame);
    // Without a dump of its own MAME gets the studio's OpenDiskSysROM.
    bios_->setText(bios.isEmpty() ? tr("OpenDiskSysROM (built in)") : tr("Your dump"));
    bios_->setToolTip(QDir::toNativeSeparators(bios));
}

void SettingsDialog::chooseMame() {
    const QString current = runtime::ToolLocator::emulatorPath(QStringLiteral("mame"));
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Choose MAME"), current, tr("Programs (*.exe);;All files (*)"));
    if (path.isEmpty()) return;
    QSettings().setValue(QString::fromLatin1(kMameKey), path);
    refresh();
}

void SettingsDialog::chooseBios() {
    const QString mame = runtime::ToolLocator::emulatorPath(QStringLiteral("mame"));
    if (mame.isEmpty()) {
        frame::MessageDialog::inform(this, tr("Settings"), tr("MAME was not found"),
                                     tr("Choose MAME first: the BIOS goes where it looks for it."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose the Disk System BIOS"), QString(),
                                                      tr("BIOS dumps (*.rom *.bin);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    const sdk::ProcessSpec spec =
        runtime::ToolLocator::kitCommand({QStringLiteral("studio"), QStringLiteral("fds-bios"),
                                          QStringLiteral("--mame"), mame, QStringLiteral("--install"), path},
                                         QFileInfo(mame).absolutePath(), &error);
    if (!spec.program.isEmpty()) {
        QProcess process;
        process.setWorkingDirectory(spec.workingDirectory);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        process.setProcessEnvironment(environment);
        process.start(spec.program, spec.arguments);
        if (!process.waitForStarted(10000)) {
            error = process.errorString();
        } else if (!process.waitForFinished(30000)) {
            process.kill();
            error = tr("The kit did not answer.");
        } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (error.startsWith(QLatin1String("error: "))) error = error.mid(7);
            if (error.isEmpty()) error = tr("exit code %1").arg(process.exitCode());
        }
    }
    if (!error.isEmpty()) {
        frame::MessageDialog::inform(this, tr("Settings"), tr("The BIOS was not installed"), error);
    }
    refresh();
}

}  // namespace rl::shell
