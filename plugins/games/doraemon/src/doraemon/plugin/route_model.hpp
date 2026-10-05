#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QVector>

namespace rl::doraemon {

class DoraemonSession;

// One World 2 route ("Part 1" ...) as the stage bytecode lays its screens
// out: panels of 16x15 metatiles in the direction the ship scrolls, each in
// its background preset. Painting a background cell writes the selector's
// logical screen (and every selector sharing its stream); enemy spawns are
// marked and stay where they are.
class DoraemonRouteModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    DoraemonRouteModel(DoraemonSession* session, int route);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    sdk::Library sheet() const override;
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {16, 16}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return metatile_; }
    void setCurrentBrush(int value) override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    QString lockReason(QPoint cell) const override;
    bool rightClickPicks() const override { return true; }
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override;
    void selectObject(const QString& id) override;

    static constexpr int kSpawnBase = 208;
    static constexpr int kPadding = 239;

private:
    // A route panel, read once from the session's World 2 view.
    struct Panel {
        int selector = 0;
        int direction = 0;
        int palette = 0;
        int x = 0;
        int y = 0;
        QVector<int> raw;       // per cell: raw y * 16 + raw x, or -1
        QVector<int> fixed;     // per cell: the padding value where raw is -1
    };
    struct Located {
        int panel = -1;
        int selector = -1;
        int raw = -1;           // raw y * 16 + raw x; -1: a fixed (padding) cell
        int fixed = kPadding;
    };
    void readPanels();
    Located locate(QPoint cell) const;
    int valueAt(const Located& at) const;
    bool commit(const QHash<QPair<int, int>, int>& cells, const QString& text, QString* error);

    DoraemonSession* session_;
    int route_;
    QString name_;
    QVector<Panel> panels_;
    QVector<int> panelAt_;      // per panel position: index into panels_, or -1
    int columns_ = 1;
    int rows_ = 1;
    QVector<QJsonObject> annotations_;
    int metatile_ = 1;
    int cell_ = -1;
    // (selector, raw) -> painted metatile
    QHash<QPair<int, int>, int> stroke_;
};

}  // namespace rl::doraemon
