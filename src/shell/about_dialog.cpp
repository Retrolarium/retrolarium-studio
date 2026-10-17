#include "shell/about_dialog.hpp"

#include "sdk/plugin.hpp"
#include "shell/project_links.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/brand_mark.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QUrl>
#include <QtGlobal>

namespace rl::shell {

namespace {

ui::Label* paragraph(const QString& text, QWidget* parent) {
    auto* label = new ui::Label(text, ui::Label::Role::Body, parent);
    label->setTone(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

// Two columns: keys on the left in monospace, what they do on the right.
QGridLayout* keyTable(const QVector<QPair<QString, QString>>& rows, QWidget* parent) {
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);
    grid->setColumnMinimumWidth(0, 120);
    for (int i = 0; i < rows.size(); ++i) {
        grid->addWidget(new ui::Label(rows.at(i).first, ui::Label::Role::Mono, parent), i, 0);
        auto* what = new ui::Label(rows.at(i).second, ui::Label::Role::Row, parent);
        what->setTone(QStringLiteral("muted"));
        grid->addWidget(what, i, 1);
    }
    grid->setColumnStretch(1, 1);
    return grid;
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : StudioDialog(tr("About"), parent) {
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    layout()->setSizeConstraint(QLayout::SetFixedSize);

    auto* content = new QVBoxLayout(body());
    content->setContentsMargins(28, 22, 28, 20);
    content->setSpacing(8);

    // The mark beside the name and version.
    auto* heading = new QHBoxLayout;
    heading->setSpacing(16);
    heading->addWidget(new ui::BrandMark(48, body()), 0, Qt::AlignTop);
    auto* names = new QVBoxLayout;
    names->setSpacing(4);
    names->addWidget(new ui::Label(tr("Retrolarium Studio"), ui::Label::Role::Title, body()));
    auto* version = new ui::Label(tr("Version %1 · Qt %2").arg(QCoreApplication::applicationVersion(),
                                                                         QString::fromLatin1(qVersion())),
                                  ui::Label::Role::Mono, body());
    version->setTone(QStringLiteral("faint"));
    names->addWidget(version);
    names->addStretch(1);
    heading->addLayout(names, 1);
    content->addLayout(heading);
    content->addSpacing(6);

    auto* intro = paragraph(tr("A studio for changing classic console games from their disassemblies: "
                               "levels, worlds, tiles and blocks, palettes, sprites and animations, "
                               "screens, text and music, each in an editor that shows it the way the "
                               "game does and keeps it within what the ROM can hold."),
                            body());
    intro->setFixedWidth(500);
    content->addWidget(intro);

    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("Games"), ui::Label::Role::Caps, body()));
    QStringList games;
    for (const auto& plugin : sdk::PluginRegistry::instance().plugins()) {
        games << plugin->displayName();
    }
    auto* gameList = paragraph(games.isEmpty() ? tr("No game plugins are built in.")
                                               : games.join(QStringLiteral(" · ")),
                               body());
    gameList->setTone(QStringLiteral("text"));
    content->addWidget(gameList);
    auto* trademarks = paragraph(tr("The games are named only to say which ones the studio works with. "
                                    "Their titles and characters are trademarks of their owners, who "
                                    "have not made or endorsed Retrolarium. You need your own copy of "
                                    "each game."),
                                 body());
    trademarks->setFixedWidth(500);
    content->addWidget(trademarks);

    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("From an edit to the game"), ui::Label::Role::Caps, body()));
    auto* loop = paragraph(tr("Every change is one undo step and stays in the project's documents until "
                              "you save. Build hands them to Retrolarium Kit, which writes a new ROM; "
                              "Play runs it from power-on, Play from selected right where "
                              "you set the start, inside the studio in MAME. A game that is already "
                              "running reloads into the new build without restarting the emulator."),
                           body());
    loop->setFixedWidth(500);
    content->addWidget(loop);

    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("Shortcuts"), ui::Label::Role::Caps, body()));
    content->addLayout(keyTable({{QStringLiteral("Ctrl+O"), tr("Open a project")},
                                 {QStringLiteral("Ctrl+S"), tr("Save all")},
                                 {QStringLiteral("Ctrl+Z  Ctrl+Y"), tr("Undo, redo")},
                                 {QStringLiteral("Ctrl+B"), tr("Build the ROM")},
                                 {QStringLiteral("Ctrl+F5"), tr("Play from power-on")},
                                 {QStringLiteral("F4"), tr("Play from selected")},
                                 {tr("Pause"), tr("Pause or resume the game")},
                                 {QStringLiteral("Shift+F5"), tr("Stop the game")},
                                 {QStringLiteral("\\"), tr("Frame advance")},
                                 {QStringLiteral("-  ="), tr("Slower, faster")},
                                 {QStringLiteral("Tab"), tr("Fast-forward while held")},
                                 {QStringLiteral("F5  F8"), tr("Save state, load state")},
                                 {QStringLiteral("F6  F7"), tr("Previous, next state slot")},
                                 {QStringLiteral("Ctrl+J"), tr("Messages panel")},
                                 {QStringLiteral("Ctrl+Shift+L"), tr("Light or dark theme")}},
                                body()));

    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("Playing"), ui::Label::Role::Caps, body()));
    content->addLayout(keyTable({{tr("Arrows"), tr("D-pad")},
                                 {QStringLiteral("Z  X"), tr("B and A (run, jump)")},
                                 {tr("Enter"), tr("Start")},
                                 {tr("Right Shift"), tr("Select")}},
                                body()));
    auto* pad = new ui::Label(tr("A game pad works too. Click the picture to give it the keyboard."),
                              ui::Label::Role::Small, body());
    content->addWidget(pad);

    // The notices GPL-3.0 section 5(d) asks an interactive program to show.
    content->addSpacing(10);
    content->addWidget(new ui::Label(tr("License"), ui::Label::Role::Caps, body()));
    const QString link = QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%3</a>");
    const QString ink = theme::current().accentInk.name();
    QString thirdParty = QStringLiteral("THIRD-PARTY.md");
    const QString notices = QDir(QCoreApplication::applicationDirPath()).filePath(thirdParty);
    if (QFileInfo::exists(notices)) {
        thirdParty = link.arg(QUrl::fromLocalFile(notices).toString(), ink, thirdParty);
    }
    auto* license = paragraph(
        tr("Copyright © 2026 Daniel Oranguthang. Retrolarium Studio is free software under the GNU "
           "General Public License, version 3 or any later version, and comes with absolutely no "
           "warranty. Its source code is at %1. The components it carries (Qt, MAME, Python, the "
           "kit's assemblers, OpenDiskSysROM and others) keep their own licenses: %2 lists them, and "
           "their texts are in the licenses folder next to the program.")
            .arg(link.arg(QString::fromLatin1(links::kSource), ink,
                          QString::fromLatin1(links::kSource).section(QLatin1Char('/'), 2)),
                 thirdParty),
        body());
    license->setTextFormat(Qt::RichText);
    license->setOpenExternalLinks(true);
    license->setFixedWidth(500);
    content->addWidget(license);

    content->addSpacing(16);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* close = new ui::PrimaryButton(tr("Close"), QString(), body());
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    content->addLayout(buttons);
}

}  // namespace rl::shell
