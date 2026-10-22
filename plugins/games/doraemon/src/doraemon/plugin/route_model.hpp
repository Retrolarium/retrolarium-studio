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
// logical screen (and every selector sharing its stream). The Objects layer
// shows the enemy spawn tokens as their sprites in the chapter's colours:
// drag one to another cell, change its enemy, add or remove one; screens
// that can drop an inventory item carry a translucent secret symbol.
class DoraemonRouteModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    DoraemonRouteModel(DoraemonSession* session, int route);

    QString title() const override;
    QString startPoint() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    QString currentLibraryItem() const override;
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
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
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;
    bool removeObject(const QString& id, QString* error) override;

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
        QVector<int> raw;    // per cell: raw y * 16 + raw x, or -1
        QVector<int> fixed;  // per cell: the padding value where raw is -1
    };
    struct Located {
        int panel = -1;
        int selector = -1;
        int raw = -1;  // raw y * 16 + raw x; -1: a fixed (padding) cell
        int fixed = kPadding;
    };
    void readPanels();
    Located locate(QPoint cell) const;
    int valueAt(const Located& at) const;
    bool commit(const QHash<QPair<int, int>, int>& cells, const QString& text, QString* error);
    bool objectLayer() const { return layer_ == QLatin1String("objects"); }
    QPoint cellOf(int index) const { return {index % gridSize().width(), index / gridSize().width()}; }
    QVector<int> secretScreens() const;
    sdk::Inspection inspectObject() const;
    bool setObjectField(const QString& key, const QVariant& value, QString* error);

    DoraemonSession* session_;
    int route_;
    QString name_;
    QVector<Panel> panels_;
    QVector<int> panelAt_;  // per panel position: index into panels_, or -1
    int columns_ = 1;
    int rows_ = 1;
    QVector<QJsonObject> annotations_;
    int metatile_ = 1;
    int cell_ = -1;
    int chapter_ = 6;  // the part's sprite preset (World 2 presets 6-8)
    QString layer_ = QStringLiteral("blocks");
    QString object_;     // "spawn:<cell>" or "secret:<slot>"
    int enemy_ = 1;      // the enemy state the place tool adds
    int dragFrom_ = -1;  // the dragged spawn's cell
    int dragTo_ = -1;
    // (selector, raw) -> painted metatile
    QHash<QPair<int, int>, int> stroke_;
};

}  // namespace rl::doraemon
