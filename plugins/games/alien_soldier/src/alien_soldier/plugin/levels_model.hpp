#pragma once

#include "sdk/editor_model.hpp"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>

namespace rl::alien_soldier {

class AlienSoldierSession;

// A level (a block set) or one stage of it: the part of the coarse map the
// stage's camera covers (the kit works it out), both planes as the game
// draws them at the stage's start (plane B lined up with plane A), and the
// stage's placed objects, each in the art it drew when the game spawned it
// (objects.json's spawns; a marker when none is recorded). A whole level
// shows every stage of the set
// on the layout they share, the rest of the map dimmed. Objects move and
// change in the inspector; the chunks layer paints the coarse map's chunk
// indices (a cell of 8 pixels stands for the 256-pixel chunk around it).
class LevelsModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    // `stage` -1 is the whole level of block set `set`; `arena` one of the
    // stage's arenas (a screen elsewhere in the map, without objects).
    LevelsModel(AlienSoldierSession* session, int set, int stage, int arena = -1);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString startPoint() const override;
    bool isReadOnly() const override;
    QString readOnlyReason() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    // A level opens at 2x from its left end, where its first stage starts.
    int openingZoom() const override { return 8; }
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;
    QString currentLibraryItem() const override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;
    // A chunk dragged from the sheet paints the coarse cell it lands on.
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    // The stages shown, and the one whose scene draws them.
    QVector<int> stages() const;
    int sceneStage() const;
    // The scene area shown (cell (0, 0) is its top left corner).
    QRect area() const;
    QPoint world(QPoint cell) const;
    QJsonObject record(int stage) const;
    QString spawnList(int stage) const;
    // The placed objects shown, as (stage, index in its spawn list).
    QVector<QPair<int, int>> placements() const;
    QJsonObject placement(int stage, int index) const;
    // What a placement draws: a mapping block's frame with an object
    // attribute (frame -1: nothing recorded).
    struct Sprite {
        QString block;
        int frame = -1;
        int attribute = 0;
    };
    Sprite spriteOf(int stage, int index) const;
    QJsonArray spritePieces(const Sprite& sprite) const;
    // The rectangle a placement covers around its position: its sprite's
    // pieces, or a 16-pixel marker.
    QRect objectRect(int stage, int index) const;
    // A placement's position in the world.
    QPoint objectPosition(int stage, int index) const;
    void drawSprites(QImage& image, const QRect& shown) const;
    // Both planes of the area shown, each stage's part from its own scene;
    // kept while the scenes, the area and the planes shown stay.
    QImage planes(const QRect& shown) const;
    // The coarse map byte a cell's world position reads, and the payload
    // value it is (-1 when no map document holds it).
    int coarseValueIndex(QPoint cell, QString* payload) const;
    int chunkCount() const;

    AlienSoldierSession* session_;
    int set_;
    int stage_;
    int arena_;
    QString layer_ = QStringLiteral("objects");
    QString selected_;
    int brush_ = 0;
    bool planeA_ = true;
    bool planeB_ = true;
    QJsonObject strokeBefore_;
    quint64 strokeRevision_ = 0;
    bool overflowReported_ = false;
    // The stage's VRAM with the tiles it leaves blank filled from a block's
    // recorded art (what the object loads itself), by "stage/block".
    mutable QHash<QString, QByteArray> spriteVram_;
    mutable QImage planes_;
    mutable QImage planesHigh_;  // where high-priority tiles cover low sprites
    mutable quint64 planesGeneration_ = 0;
    mutable QRect planesArea_;
    mutable int planesShown_ = -1;
};

}  // namespace rl::alien_soldier
