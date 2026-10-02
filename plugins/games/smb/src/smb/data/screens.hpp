#pragma once

#include "smb/data/level_records.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace rl::smb {

// The glyphs of a fixed-width text encoding (nes.fixed-text-fields.v1, and
// the text fields of screen packets): each character has one tile code.
struct SmbTextEncoding {
    QString characters;
    QVector<int> codes;
    QString caseFold;       // "upper", "lower" or "none"
    int padding = 0;        // the code that fills a field after its text

    static SmbTextEncoding fromJson(const QJsonObject& json);
    bool isValid() const { return !characters.isEmpty(); }

    QString fold(const QString& text) const;
    int code(QChar character) const;        // -1 when unmapped
    QChar character(int code) const;        // null when unmapped
    // The text of a field's codes, the padding stripped from the right;
    // unmapped codes show as '?'.
    QString decode(const QVector<int>& codes) const;
    // The codes of `text` padded to `width`, or nullopt with the reason.
    std::optional<QVector<int>> encode(const QString& text, int width, QString* error) const;
    // "0-9 A-Z space - !"
    QString summary() const;
};

// One PPU write of a display list: `length` bytes from `address`, across or
// down (32 apart), each its own value or one value repeated.
struct SmbScreenCommand {
    int address = 0;
    bool vertical = false;
    bool repeat = false;
    int length = 0;
    QVector<int> values;

    int target(int index) const { return address + index * (vertical ? 32 : 1); }
    int valueIndex(int index) const { return repeat ? 0 : index; }
    int bytes() const { return 3 + values.size(); }
};

// The bytes all screens of a pooled resource take of the range they share.
struct SmbScreenPool {
    bool pooled = false;
    int used = 0;
    int size = 0;
};

// A text field: `width` tiles of one screen row from tile `start`.
struct SmbScreenTextField {
    QString key;
    QString label;
    QString screen;
    int start = 0;
    int width = 0;
};

// One screen of a nes.ppu-screen-packets.v1 resource: the display list the
// game writes, and the name table that leaves (tiles row-major from the
// commands over the fill tile, 64 attribute bytes). Only what a command
// writes can change, and the list keeps its byte size.
struct SmbScreen {
    QString key;
    QString label;
    int capacity = 0;
    int width = 32;
    int height = 30;
    int base = 0x2000;          // name table address
    QVector<int> tiles;
    QVector<int> attributes;
    QVector<SmbScreenCommand> commands;
    QJsonArray trailing;        // bytes after the terminator, kept as they are

    int attributeBase() const { return base + width * height; }
    // The display list's bytes: its writes and the terminator.
    int listBytes() const;
    // The command and value index that last writes a tile (an attribute
    // byte); {-1, -1} when nothing does.
    QPoint tileWriter(int tile) const;
    QPoint attributeWriter(int index) const;
    // The tiles a repeated command writes along with this one (itself
    // included); one tile otherwise.
    QVector<int> runOf(int tile) const;
    // The palette (0-3) of a tile from its attribute byte.
    int paletteOf(int tile) const;
    int attributeIndexOf(int tile) const;
    int attributeShiftOf(int tile) const;
    // The tile the name table holds where nothing writes (the most common
    // unwritten tile; $24, blank, when every cell is written).
    int fillTile() const;
    // The name-table cells a command writes, in order; empty for a write to
    // the attribute table.
    QVector<int> cellsOf(int command) const;
};

// A whole screen-packet resource.
class SmbScreenSet {
public:
    static SmbScreenSet fromJson(const QJsonObject& resource);
    // The resource with the screens written back; other keys stay.
    QJsonObject toJson(QJsonObject resource) const;

    QVector<SmbScreen> screens;
    QVector<SmbScreenTextField> textFields;
    SmbTextEncoding encoding;   // invalid when the resource has no text
    // A pooled resource (capacity_pool) packs its screens one after another
    // into the range they share, so a list may grow while the others leave
    // room; the growable fields may lengthen their text.
    bool pooled = false;
    QStringList growable;

    SmbScreenPool pool() const;
    // Whether a field's length can change: pooled, listed, and exactly one
    // write across its row, so its text sets that write's length.
    bool canGrow(const SmbScreenTextField& field) const;
    // The longest text a field takes: its width, or to the end of its row.
    int maxWidth(const SmbScreenTextField& field) const;

    int indexOf(const QString& key) const;
    // Changes the value that writes a tile (a repeated command changes its
    // whole run) or an attribute byte. Throws SmbError when no command
    // writes it.
    void setTile(int screen, int tile, int value);
    void setAttribute(int screen, int index, int value);
    // Sets the palette of the 16x16 block a tile is in.
    void setPalette(int screen, int tile, int palette);
    // Moves a name-table write to start at another cell, across or down;
    // the display list keeps its size. Cells it leaves show the fill tile.
    // Throws SmbError when the write would leave the screen or wrap a row.
    void moveCommand(int screen, int command, int cell, bool vertical);

    QString text(const SmbScreenTextField& field) const;
    void setText(const SmbScreenTextField& field, const QString& text);   // throws SmbError

private:
    void project(SmbScreen& screen) const;
    int writerOf(const SmbScreenTextField& field) const;
    void resizeText(const SmbScreenTextField& field, const QString& text);
};

class SmbResourceDocument;

// One piece of game text: a fixed text field, or a text field of a screen.
struct SmbTextSlot {
    QString resource;       // resource id
    QString key;            // field key
    QString label;          // "Brother name"
    QString screen;         // screen key; empty for a fixed text field
    QString screenLabel;    // "Status bar"
    int width = 0;
    int maxWidth = 0;       // more than width when the text can grow
};

// The shared range of the pooled screen resources; not pooled when none is.
SmbScreenPool screenPool(const SmbResourceDocument& document);

// Every text slot of the document, fixed fields first.
QVector<SmbTextSlot> textSlots(const SmbResourceDocument& document);
SmbTextEncoding slotEncoding(const SmbResourceDocument& document, const SmbTextSlot& slot);
QString slotText(const SmbResourceDocument& document, const SmbTextSlot& slot);
// Throws SmbError when the text does not fit or the font lacks a character.
void setSlotText(SmbResourceDocument& document, const SmbTextSlot& slot, const QString& text);

}  // namespace rl::smb
