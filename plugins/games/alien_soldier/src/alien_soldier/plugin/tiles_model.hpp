#pragma once

#include "alien_soldier/data/documents.hpp"
#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One art payload's 4bpp tiles, compressed (LZSS or the tile codec, packed
// again into the payload's slot on build) or not. The pens are a CRAM line
// of a palette list the user picks, since the art does not say which.
class TilesModel final : public sdk::TileModel {
    Q_OBJECT

public:
    TilesModel(AlienSoldierSession* session, Doc doc, const QString& label);

    QString title() const override { return label_; }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    int tileCount() const override;
    int penCount() const override { return 16; }
    QVector<QColor> penColors() const override;
    QVector<quint8> tilePixels(int tile) const override;
    bool isTileDirty(int tile) const override;
    int currentTile() const override { return tile_; }
    void setCurrentTile(int tile) override;
    int currentPen() const override { return pen_; }
    void setCurrentPen(int pen) override;

    void beginStroke() override;
    bool setPixel(QPoint pixel, int pen) override;
    void endStroke() override;
    bool fill(QPoint pixel, int pen, QString* error) override;

private:
    QStringList tiles(const QJsonObject& document) const;
    // Writes tile `index` of the payload in `document`.
    void setTile(QJsonObject& document, int index, const QString& hex) const;

    AlienSoldierSession* session_;
    Doc doc_;
    QString label_;
    int tile_ = 0;
    int pen_ = 1;
    QString paletteList_;
    int line_ = 0;
    QJsonObject strokeBefore_;
    quint64 strokeRevision_ = 0;
};

}  // namespace rl::alien_soldier
