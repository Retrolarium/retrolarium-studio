#pragma once

#include <QColor>
#include <QImage>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

// Plain data exchanged between the studio and game plugins. Nothing here knows
// about a particular platform or game.
namespace rl::sdk {

// The universal editors of the studio. A data kind names the one that edits it.
enum class EditorType { None, TileMap, Tile, Palette, Composition, Table, Sequencer, Emulator };

// Class-style name shown in the editor badge ("TileMapEditor").
QString editorName(EditorType type);

// Identity of an open project, shown in the global toolbar and status bar.
struct Profile {
    QString name;       // "Flicky"
    QString badge;      // platform badge: "MD", "NES"
    QString platform;   // "Mega Drive"
    QString revision;   // "UE"
    QString cpu;        // "68000"
    QString chip;       // sound chip: "YM2612 + PSG"
    QString codec;      // codec tag shown with engine data
};

// One entry of the kind rail.
struct Kind {
    QString id;         // "levels"
    QString label;      // "Levels"
    QString icon;       // theme icon name
    EditorType editor = EditorType::None;
};

struct DocumentEntry {
    QString id;
    QString label;
    QString meta;       // faint note on the right: "bonus", "48 tiles"
    bool dirty = false;
};

struct DocumentGroup {
    QString title;
    QVector<DocumentEntry> documents;
};

enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Info;
    QString source;     // "levels/round07"
    QString message;
    QString location;   // "Round 07 · 30,10"
    QString kind;       // where a click navigates, optional
    QString document;
};

// Byte or length budget of the open document ("layout 1120 / 1120 B").
struct Budget {
    QString text;
    qint64 used = 0;
    qint64 capacity = 0;
    bool isValid() const { return capacity > 0 || !text.isEmpty(); }
};

// One field of the inspector, generated from the selection's record.
struct Field {
    enum class Type { Text, Integer, Choice, ReadOnly };

    QString key;
    QString label;
    Type type = Type::ReadOnly;
    QVariant value;
    QString hint;           // unit or range at the right edge
    QStringList choices;    // Choice
    QVector<QImage> choiceIcons;    // Choice: optional preview per choice
    int minimum = 0;        // Integer
    int maximum = 0;
    bool monospace = false;
};

struct LibraryItem {
    QString id;
    QString label;
    QString hint;
    QImage icon;            // optional preview, drawn scaled in the library
};

// Items to place or paint with. A model returns it from
// EditorModel::sheet() to show it beside the canvas, or in
// Inspection::library to show it in the inspector; both behave the same.
struct Library {
    QString hint;           // "drag onto the map"
    // A sheet of tiles without labels (pattern tables) instead of labelled
    // entries: `columns` to a row, each cell shaped like `cell` (8x16 for
    // sprite pattern pairs).
    bool grid = false;
    int columns = 16;
    QSize cell{1, 1};
    QVector<LibraryItem> items;
};

struct EngineRow {
    QString key;
    QString value;
};

// What the inspector shows for the current selection.
struct Inspection {
    QString kicker;         // "ROUND OBJECT"
    QString title;          // "Flicky / start"
    QVector<Field> fields;
    QString note;
    Library library;
    QVector<EngineRow> engine;
    QString engineTag;      // codec tag in the Engine data header
    QString preview;        // text as the game shows it, on a black box
};

// A view setting shown as a chip ("Collision On"). View options never change
// document data.
struct ViewOption {
    QString key;
    QString label;
    QString value;
    QStringList choices;    // drop-down choices; empty with toggle = on/off
    bool toggle = false;
};

// A program the host runs for a plugin (kit commands, emulators, renderers).
struct ProcessSpec {
    QString label;              // shown in Build output: "Build ROM"
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;   // added to the studio's environment
    // Emulators: a file the emulator writes once the game is up (console
    // output arrives buffered, too late to tell). Until it appears the
    // Playtest view shows that the emulator is starting. Empty: up shortly
    // after the process starts.
    QString readyFile;
    // Or a Lua expression the studio checks each frame instead, with
    // `memory` the main CPU's program space ("memory:read_u8(0x0770) == 1");
    // true for a few frames in a row means the game is up.
    QString readyWhen;
    // Emulators: the colours the plugin's editors draw with, as 0xRRGGBB for
    // the first entries of the machine's palette device (NES: its 64
    // colours), so the game can be shown in the studio's colours.
    QVector<QRgb> palette;
};

struct ProcessResult {
    bool started = false;
    bool crashed = false;
    bool cancelled = false;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    QString error;              // why it did not start or finish

    bool succeeded() const { return started && !crashed && !cancelled && exitCode == 0; }
};

// An editor tool shown in the tool segment.
struct Tool {
    QString id;             // "select", "paint", "erase", "place"
    QString label;
    QString icon;
};

// A command an editor offers as a button in its toolbar row ("Loop demo",
// "Record").
struct EditorAction {
    QString id;
    QString label;
    QString icon;
    QString tooltip;
    bool danger = false;        // red: it replaces data
    bool showsPlaytest = false; // the Playtest view comes forward after it
    // Asked first when set: the dialog's heading, text and confirm button.
    QString confirmHeading;
    QString confirmText;
    QString confirmAction;
};

}  // namespace rl::sdk
