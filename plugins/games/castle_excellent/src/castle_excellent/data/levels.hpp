#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QVector>

#include <stdexcept>

namespace rl::castle_excellent {

class CastleExcellentError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A static object of a room: its encoded type and where it stands, x in
// 16-pixel cells, y in 8-pixel rows.
struct CastleExcellentObject {
    int type = 0;
    int x = 0;
    int y = 0;

    bool operator==(const CastleExcellentObject& other) const { return type == other.type && x == other.x && y == other.y; }
    bool operator!=(const CastleExcellentObject& other) const { return !(*this == other); }
};

// How the game draws an object type: runtime type, its pair of metasprite
// descriptors and the x offset of each 8x16 sprite.
struct CastleExcellentObjectPreview {
    int runtimeType = 0;
    int descriptorOffset = 0;   // in bytes: two per sprite (tile, attributes)
    QVector<int> xOffsets;
};

// The room document (retrolarium.castle-excellent-levels): 100 rooms of 20x20 logical
// cells (16x8 pixels: two background tiles) and their static objects, plus
// the tables the game draws them with.
class CastleExcellentLevels {
public:
    static CastleExcellentLevels load(const QString& path);  // throws CastleExcellentError
    void save(const QString& path) const;

    int width() const { return width_; }
    int height() const { return height_; }
    int roomCount() const;
    int roomNumber(int room) const;
    QString revision() const;

    QVector<int> cells(int room) const;             // row-major
    void setCell(int room, QPoint cell, int value); // throws CastleExcellentError
    QVector<CastleExcellentObject> objects(int room) const;
    void setObjects(int room, const QVector<CastleExcellentObject>& objects);

    // The rendering tables (castle-excellent.room-rendering.v1).
    int chrBank() const;
    int renderingCellCount() const;
    QVector<int> renderingInts(const QString& key) const;
    QVector<CastleExcellentObjectPreview> objectPreviews() const;
    int encodedTypeCount() const;   // runtime_object_types entries

    bool operator==(const CastleExcellentLevels& other) const { return root_ == other.root_; }
    bool operator!=(const CastleExcellentLevels& other) const { return root_ != other.root_; }

private:
    QJsonObject room(int index) const;
    void setRoom(int index, const QJsonObject& room);

    QJsonObject root_;
    int width_ = 20;
    int height_ = 20;
};

}  // namespace rl::castle_excellent
