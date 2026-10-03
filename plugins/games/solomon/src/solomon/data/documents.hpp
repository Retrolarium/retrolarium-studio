#pragma once

#include "nes/bundle.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QVector>

#include <stdexcept>

namespace rl::solomon {

class SolomonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

QJsonObject loadJson(const QString& path);              // throws SolomonError
void saveJson(const QString& path, const QJsonObject& root);

constexpr int kWidth = 16;
constexpr int kHeight = 12;
constexpr int kItemArenaBytes = 1342;    // item streams of all rooms
constexpr int kEnemyArenaBytes = 726;    // enemy streams of all rooms
constexpr int kMaximumEnemies = 17;      // the runtime enemy pool

struct SolomonEnemy {
    int type = 0x18;
    QPoint position;
    bool operator==(const SolomonEnemy& o) const { return type == o.type && position == o.position; }
};

// The rooms document (retrolarium.solomon-rooms): 53 rooms of 16x12
// cells with two block planes (brown, breakable; white, solid), an item
// stream (header with the door, key, mirrors and player start, then item
// commands) and an enemy stream.
class SolomonRooms {
public:
    SolomonRooms() = default;
    explicit SolomonRooms(const QJsonObject& root) : root_(root) {}
    const QJsonObject& root() const { return root_; }

    int roomCount() const;
    int roomNumber(int room) const;
    QString revision() const { return root_.value(QStringLiteral("revision")).toString(); }

    bool block(int room, QPoint cell, bool white) const;
    void setBlock(int room, QPoint cell, bool white, bool on);

    QJsonArray itemCommands(int room) const;
    void setItemCommands(int room, const QJsonArray& commands);
    QJsonObject metadata(int room) const;
    void setMetadata(int room, const QJsonObject& metadata);
    QPoint metadataPosition(int room, const QString& key) const;   // (-1,-1) when off the room
    int spawnLifetime(int room) const;
    QVector<SolomonEnemy> enemies(int room) const;
    void setEnemies(int room, const QVector<SolomonEnemy>& enemies);
    QJsonObject specialRooms() const { return root_.value(QStringLiteral("special_rooms")).toObject(); }

    // The CHR bank a room draws with: bits 2-3 of its terminator.
    static int chrBank(const QJsonArray& commands);
    static int itemStreamBytes(const QJsonArray& commands);
    int itemBytes() const;                  // all rooms
    int enemyBytes() const;                 // all rooms

    bool operator==(const SolomonRooms& other) const { return root_ == other.root_; }
    bool operator!=(const SolomonRooms& other) const { return root_ != other.root_; }

private:
    QJsonObject room(int index) const;
    void setRoom(int index, const QJsonObject& room);

    QJsonObject root_;
};

// One 16x16 room pattern: four background tiles and a palette.
struct SolomonPattern {
    int palette = 0;
    int tiles[4] = {0, 0, 0, 0};
};

// Every document of a Solomon's Key project, swapped whole by undo.
struct SolomonState {
    SolomonRooms rooms;
    QJsonObject art;            // retrolarium.solomon-room-art
    QJsonObject animations;     // retrolarium.solomon-animations
    nes::Bundle graphics;

    bool operator==(const SolomonState& other) const {
        return rooms == other.rooms && art == other.art && animations == other.animations &&
               graphics == other.graphics;
    }
};

SolomonPattern pattern(const QJsonObject& art, int index);
int patternCount(const QJsonObject& art);
int groupColor(const QJsonObject& art, int group);
// The sprite frame (left_tile, right_tile, flags) the game shows first for
// an enemy type ($18-$83) or for Wizard; empty when the tables do not say.
QJsonObject enemyFrame(const QJsonObject& animations, int type);
QJsonObject wizardFrame(const QJsonObject& animations, bool facesLeft);

QPoint point(const QJsonValue& value);
QJsonObject pointJson(QPoint point);

}  // namespace rl::solomon
