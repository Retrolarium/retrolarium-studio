#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QPoint>
#include <QVector>

namespace rl::castle {

class CastleSession;

// The metasprite descriptors (castle.metasprite-descriptors.v1): the 8x16
// sprites, one tile pair and one attribute byte each, that the player's and
// the objects' frames are drawn from. The sheet lays the frames the game's
// code points at side by side, one 8x16 sprite to a column; painting puts
// the brush pattern pair into a sprite, the inspector edits a frame.
class CastleSpriteModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    explicit CastleSpriteModel(CastleSession* session);

    QString title() const override { return QStringLiteral("Sprites"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return tile_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

    // A run of descriptors drawn together: a frame the code points at, or a
    // descriptor nothing points at.
    struct Frame {
        int first = 0;
        int count = 0;
        bool player = false;
        bool referenced = true;
        QPoint at;          // sheet column (in sprites) and row (in 16-pixel rows)
    };

private:
    int count() const;
    int descriptorAt(QPoint cell) const;
    int tileOf(int descriptor) const;
    int attributesOf(int descriptor) const;
    QString frameLabel(int frame) const;
    bool setDescriptors(const QHash<int, QPair<int, int>>& values, const QString& text, QString* error);

    CastleSession* session_;
    QVector<Frame> frames_;
    QHash<int, QPoint> positions_;      // descriptor -> sheet column, row
    int rows_ = 1;
    int selected_ = -1;                 // frame
    int tile_ = 0x90;
    int libraryPalette_ = 0;
    bool stroking_ = false;
    QHash<int, int> pending_;           // descriptor -> tile
};

}  // namespace rl::castle
