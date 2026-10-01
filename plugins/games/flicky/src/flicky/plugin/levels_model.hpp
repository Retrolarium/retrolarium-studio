#pragma once

#include "flicky/data/level_document.hpp"
#include "sdk/editor_model.hpp"

#include <optional>

namespace rl::flicky {

class FlickySession;

// One Flicky round in the universal tile-map editor. Layers: Objects (actors,
// doors, collision flags), Layout (the 32x28 collision grid) and Background
// (scenery groups). Layouts are shared between rounds, so edits show in every
// round that uses the same layout.
class LevelsModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    LevelsModel(FlickySession* session, int round);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    bool isReadOnly() const override;
    QString readOnlyReason() const override;
    QString statusText() const override;
    QString currentLibraryItem() const override { return libraryItem_; }
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override;
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override;
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override { return brush_; }
    void setCurrentBrush(int value) override;
    bool showsCellOverlay() const override;
    int cellValue(QPoint cell) const override;
    bool isCellEditable(QPoint cell) const override;
    void beginStroke() override;
    bool paintCell(QPoint cell, int value) override;
    void endStroke() override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    bool removeObject(const QString& id, QString* error) override;

private:
    struct Ref {
        QString field;
        int index = -1;
        bool isValid() const { return index >= 0; }
    };
    static Ref parseId(const QString& id);
    static QString makeId(const QString& field, int index);

    QStringList layerFields() const;
    int layoutFor(const QString& field) const;
    QString roundList(const QVector<int>& rounds) const;
    sdk::Inspection inspectRound() const;
    sdk::Inspection inspectLayout() const;
    sdk::Inspection inspectObject(const Ref& ref) const;
    sdk::Library library() const;

    FlickySession* session_;
    int round_;
    QString selected_;
    QString libraryItem_;
    int brush_ = 1;
    std::optional<FlickyLevelDocument> before_;  // open stroke or drag
    bool changed_ = false;
};

}  // namespace rl::flicky
