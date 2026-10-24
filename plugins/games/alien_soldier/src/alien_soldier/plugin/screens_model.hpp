#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>

namespace rl::alien_soldier {

class AlienSoldierSession;

// A static screen (title, options, story background) as its planes draw
// it. Painting puts the brush's name-table word (tile, palette line, flips,
// priority) where the cell's plane reads its pattern; the right button takes
// a cell's word as the brush. A word a stage's map also holds changes there.
class ScreensModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    ScreensModel(AlienSoldierSession* session, const QString& id);

    static QString screenTitle(const QString& id);

    QString title() const override { return screenTitle(id_); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override;
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;
    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;
    QString currentLibraryItem() const override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

private:
    int index() const;
    int wordOffset(QPoint cell) const;
    // A credits scene: its map is a compressed payload in a plane's name table.
    bool isTable() const;
    QString tableSource() const;

    AlienSoldierSession* session_;
    QString id_;
    int plane_ = 0;
    int brush_ = 0;
    QJsonObject beforeMaps_;
    QJsonObject beforeScreens_;
    QJsonObject beforeCompressed_;
    quint64 revisionMaps_ = 0;
    quint64 revisionScreens_ = 0;
    quint64 revisionCompressed_ = 0;
};

}  // namespace rl::alien_soldier
