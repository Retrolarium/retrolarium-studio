#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace rl::solomon {

// The game's font: digits $00-$09, letters $0A-$23, space $24 and a few
// marks. Tiles without a character show as {XX}.
QString tilesToText(const QVector<int>& tiles);
// Empty with `error` set when the text has a character the font lacks.
QVector<int> textToTiles(const QString& text, QString* error);

// One run of text the game draws, in the retrolarium.solomon-text document.
//   s:<program>:<command>  a literal write of a static PPU program
//   i:<n>                  an initial ending message
//   m:<n>                  an ending message
//   r:<run>                a run of the room intro template
//   n:<n>                  a special-room name (room intro)
struct TextString {
    QString id;
    QString label;          // what it is: "HUD labels", "Ending message 3"
    QVector<int> tiles;
    QVector<int> addresses; // PPU addresses; empty for special-room names
    bool vertical = false;
    int width = 0;          // fixed width, 0 when it may change length
    bool movable = false;
};

class SolomonText {
public:
    SolomonText() = default;
    explicit SolomonText(const QJsonObject& root) : root_(root) {}

    const QJsonObject& root() const { return root_; }
    bool isEmpty() const { return root_.isEmpty(); }
    bool operator==(const SolomonText& other) const { return root_ == other.root_; }

    QVector<TextString> strings() const;
    TextString string(const QString& id) const;
    // The text's tiles; the length may change unless the string is fixed.
    void setTiles(const QString& id, const QVector<int>& tiles);
    // Moves every address of a movable string by `delta` name-table cells.
    void move(const QString& id, int delta);
    // The 16 colours a static palette program (01, 05, 13) writes.
    QVector<int> palette(int program) const;
    void setPaletteEntry(int program, int entry, int value);

    // Bytes the regions use and hold.
    struct Usage {
        int used = 0;
        int capacity = 0;
    };
    Usage staticUsage() const;
    Usage initialUsage() const;
    Usage messageUsage() const;
    // Why the text does not fit its regions; empty when it does.
    QString overflow() const;

private:
    QJsonObject root_;
};

}  // namespace rl::solomon
