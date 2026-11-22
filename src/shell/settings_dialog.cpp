#include "shell/settings_dialog.hpp"

#include "runtime/process_runner.hpp"
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
#include <QPointer>
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

SettingsDialog::SettingsDialog(QWidget* parent, runtime::ProcessRunner* processes)
    : StudioDialog(tr("Settings"), parent), processes_(processes) {
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
    chooseBios_ = button(tr("Choose…"), body());
    connect(chooseBios_, &QPushButton::clicked, this, &SettingsDialog::chooseBios);
    grid->addWidget(chooseBios_, 1, 2);
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
    sdk::ProcessSpec spec =
        runtime::ToolLocator::kitCommand({QStringLiteral("studio"), QStringLiteral("fds-bios"),
                                          QStringLiteral("--mame"), mame, QStringLiteral("--install"), path},
                                         QFileInfo(mame).absolutePath(), &error);
    if (spec.program.isEmpty() || !processes_) {
        frame::MessageDialog::inform(this, tr("Settings"), tr("The BIOS was not installed"),
                                     error.isEmpty() ? tr("The kit is not available here.") : error);
        return;
    }
    if (processes_->isBusy()) {
        frame::MessageDialog::inform(this, tr("Settings"), tr("The BIOS was not installed"),
                                     tr("Wait for the running task to finish, then choose the BIOS again."));
        return;
    }
    // Through the studio's runner, not a wait here: the dialog stays live and
    // the kit gets the studio's environment (RETROLARIUM_CORE).
    spec.label = tr("Install the Disk System BIOS");
    chooseBios_->setEnabled(false);
    bios_->setText(tr("Installing…"));
    const QPointer<SettingsDialog> self(this);
    processes_->run(spec, [self](const sdk::ProcessResult& result) {
        if (!self) return;
        self->chooseBios_->setEnabled(true);
        if (!result.succeeded() && !result.cancelled) {
            // A run stopped by the runner (a timeout) carries its reason.
            QString problem = result.started ? result.error : QString();
            if (problem.isEmpty()) {
                problem = QString::fromUtf8(result.standardError).trimmed();
                if (problem.startsWith(QLatin1String("error: "))) problem = problem.mid(7);
            }
            if (problem.isEmpty()) problem = result.error;
            if (problem.isEmpty()) problem = tr("exit code %1").arg(result.exitCode);
            frame::MessageDialog::inform(self, tr("Settings"), tr("The BIOS was not installed"), problem);
        }
        if (self) self->refresh();
    });
}

}  // namespace rl::shell
