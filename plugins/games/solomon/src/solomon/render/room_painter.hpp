#pragma once

#include "solomon/data/documents.hpp"

#include <QHash>
#include <QImage>

namespace rl::solomon {

// Draws Solomon's Key rooms as the game builds them: 16x16 cells from the
// room-art patterns of the room's CHR bank, coloured by its palette group;
// items, the door, key and mirrors set cells, hidden items show faintly
// through their block; enemies and Wizard are 16x16 sprites.
class SolomonPainter {
public:
    static constexpr int kCell = 16;

    explicit SolomonPainter(const SolomonState* state) : state_(state) {}

    struct Options {
        bool items = true;
        bool enemies = true;
        bool start = true;
        int bonusLayout = 0;    // room 51's random bonus layout
    };

    // `draft` draws rooms an editor holds while a drag or stroke lasts.
    QImage room(int index, const Options& options, const SolomonRooms* draft = nullptr) const;
    QImage room(int index) const { return room(index, Options()); }
    // A pattern as an icon in a room's colours (16x16).
    QImage patternIcon(int pattern, int bank, int group, bool transparentZero = false) const;
    // An enemy type's first frame (16x16, transparent around), or null.
    QImage enemyIcon(int type, int bank) const;
    QImage wizardIcon(int bank, bool facesLeft) const;
    // A sprite frame record (left_tile, right_tile, flags) as 16x16.
    QImage frameImage(const QJsonObject& frame, int bank) const;
    // An 8x16 sprite pattern pair by its OAM tile byte (bit 0 picks the
    // pattern table), in sprite palette `palette`.
    QImage spritePair(int tile, int bank, int palette) const;
    int groupOf(int room) const { return room == 50 ? 0 : room / 4; }

private:
    struct Bank {
        QVector<quint8> pixels;     // 512 tiles, 64 pens each
    };
    const Bank& bank(int index) const;
    QVector<int> palette(int group) const;
    void drawPattern(QImage& image, int x, int y, const SolomonPattern& pattern, const QVector<int>& palette,
                     const Bank& bank, int opacity = 255, bool transparentZero = false) const;
    void drawSprite(QImage& image, const QJsonObject& frame, const Bank& bank, int x, int y,
                    bool mirrorY = false) const;

    const SolomonState* state_;
    mutable QHash<int, Bank> banks_;
};

}  // namespace rl::solomon
