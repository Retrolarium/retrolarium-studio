#pragma once

#include "alien_soldier/data/sprites.hpp"
#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QPair>

namespace rl::alien_soldier {

class AlienSoldierSession;

// "$1A2 · 2×4": a metatile's first tile in VRAM (relative without a known
// context) and its size in tiles.
QString metatileLabel(const Metatile& metatile, const SpriteContext& context);

// A canvas showing one sprite mapping, drawn with what its block draws with
// in the game (objects.json): the views of objects and of animations build
// on it. Pieces move with the mouse and change in the inspector; a static
// piece whose block the game repository records nothing for draws as its
// outline, coloured from the view's palette choice otherwise.
class MappingCanvas : public sdk::CompositionModel {
    Q_OBJECT

public:
    explicit MappingCanvas(AlienSoldierSession* session);

    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;

    QSize canvasSize() const override { return {256, 256}; }
    QImage render() const override;
    QPoint origin() const override { return {128, 128}; }
    QRect focusRect() const override;

    QVector<sdk::CompositionPiece> pieces() const override;
    QString selectedPiece() const override { return piece_; }
    void selectPiece(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragPiece(const QString& id, QPoint offset) override;
    void endDrag() override;

    // The object's metatiles (what the pieces of its file draw, see
    // Metatile) and the art its file's dynamic pieces upload, to put into
    // pieces: a click makes the selected piece draw one, a drag the piece it
    // lands on. The sheet stays while frames change.
    sdk::Library sheet() const override;
    QString currentLibraryItem() const override;
    void setCurrentLibraryItem(const QString& id) override;
    bool dropItem(const QString& libraryItem, const QString& piece, QString* error) override;

protected:
    // The mapping shown: (block index, mapping index) in the mapping document,
    // or block -1.
    virtual QPair<int, int> mapping() const = 0;
    QString blockLabel() const;
    // The block whose object the sheet offers the metatiles of: the one
    // shown.
    virtual QString objectBlock() const { return blockLabel(); }
    QJsonArray mappingPieces() const;
    QJsonObject pieceObject(int index) const;
    SpriteContext context() const;
    // The selected piece's fields and note, and changing one of them.
    void inspectPiece(sdk::Inspection& inspection) const;
    bool setPieceField(const QString& key, const QVariant& value, QString* error);
    void clearPiece() { piece_.clear(); }
    // Makes a static piece draw `metatile`; dynamic pieces keep their art.
    bool setPieceMetatile(int index, const Metatile& metatile, QString* error);
    // Makes a dynamic piece upload `art` instead, art of its own size.
    bool setPieceArt(int index, const QString& art, QString* error);

    AlienSoldierSession* session_;

private:
    QString piece_;
    QString paletteList_;
    int line_ = 0;
    QJsonObject dragBefore_;
    quint64 dragRevision_ = 0;
    QPoint dragStart_;
};

}  // namespace rl::alien_soldier
