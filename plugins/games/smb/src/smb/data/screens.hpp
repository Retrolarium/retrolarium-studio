#pragma once

#include "smb/data/level_records.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QString>
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

    int indexOf(const QString& key) const;
    // Changes the value that writes a tile (a repeated command changes its
    // whole run) or an attribute byte. Throws SmbError when no command
    // writes it.
    void setTile(int screen, int tile, int value);
    void setAttribute(int screen, int index, int value);
    // Sets the palette of the 16x16 block a tile is in.
    void setPalette(int screen, int tile, int palette);

    QString text(const SmbScreenTextField& field) const;
    void setText(const SmbScreenTextField& field, const QString& text);   // throws SmbError

private:
    void project(SmbScreen& screen) const;
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
};

// Every text slot of the document, fixed fields first.
QVector<SmbTextSlot> textSlots(const SmbResourceDocument& document);
SmbTextEncoding slotEncoding(const SmbResourceDocument& document, const SmbTextSlot& slot);
QString slotText(const SmbResourceDocument& document, const SmbTextSlot& slot);
// Throws SmbError when the text does not fit or the font lacks a character.
void setSlotText(SmbResourceDocument& document, const SmbTextSlot& slot, const QString& text);

}  // namespace rl::smb
