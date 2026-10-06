#include "shell/about_dialog.hpp"

#include "sdk/plugin.hpp"
#include "theme/typography.hpp"
#include "widgets/brand_mark.hpp"
#include "widgets/label.hpp"
#include "widgets/primary_button.hpp"

#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
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

AboutDialog::AboutDialog(QWidget* parent) : StudioDialog(QStringLiteral("About"), parent) {
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
    content->addLayout(keyTable({{tr("Ctrl+O"), tr("Open a project")},
                                 {tr("Ctrl+S"), tr("Save all")},
                                 {tr("Ctrl+Z  Ctrl+Y"), tr("Undo, redo")},
                                 {tr("Ctrl+B"), tr("Build the ROM")},
                                 {tr("Ctrl+F5"), tr("Play from power-on")},
                                 {QStringLiteral("F5"), tr("Play from selected")},
                                 {tr("Pause"), tr("Pause or resume the game")},
                                 {tr("Shift+F5"), tr("Stop the game")},
                                 {tr("Ctrl+J"), tr("Messages panel")},
                                 {tr("Ctrl+Shift+L"), tr("Light or dark theme")}},
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

    content->addSpacing(16);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* close = new ui::PrimaryButton(tr("Close"), QString(), body());
    connect(close, &QAbstractButton::clicked, this, &QDialog::accept);
    buttons->addWidget(close);
    content->addLayout(buttons);
}

}  // namespace rl::shell
