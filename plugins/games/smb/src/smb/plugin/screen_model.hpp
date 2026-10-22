#pragma once

#include "sdk/editor_model.hpp"
#include "smb/data/resource_document.hpp"
#include "smb/data/screens.hpp"

#include <functional>
#include <optional>

namespace rl::smb {

class SmbSession;

// One screen of a PPU screen-packet resource (status bar, lives screen, warp
// zone): the name table it leaves, painted tile by tile or palette by
// palette. Only cells the display list writes can change; a repeated write
// changes its whole run.
class SmbScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SmbScreenModel(SmbSession* session, const QString& resource, const QString& key);

    QString title() const override;
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
    QVector<sdk::MapLayer> layers() const override;
    QString currentLayer() const override { return layer_; }
    void setCurrentLayer(const QString& id) override;
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::CellBrush> brushes() const override;
    int currentBrush() const override;
    void setCurrentBrush(int value) override;
    bool showsCellOverlay() const override { return layer_ == QLatin1String("palettes"); }
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

private:
    const SmbScreenSet& set() const;
    const SmbScreen& screen() const;
    int screenIndex() const;
    int backdrop() const;
    // Applies a change to the screen set as one undo step.
    bool editSet(const QString& text, const std::function<void(SmbScreenSet&)>& change, QString* error);
    sdk::Library library() const;
    sdk::Inspection inspectWrites() const;
    // Writes the stroke or drag copy into the document as one step.
    void commitCopy(const QString& text);
    QString writeLabel(int command) const;

    SmbSession* session_;
    QString resource_;
    QString key_;
    QString layer_ = QStringLiteral("tiles");
    QString packet_;
    bool showWritten_ = true;
    int cell_ = -1;   // selected cell, -1: none
    int write_ = -1;  // selected write (Writes layer), -1: none
    bool dragging_ = false;
    int tile_ = 0x24;  // tile the paint tool lays
    int palette_ = 0;  // palette the palette layer paints

    // A stroke paints into a copy of the screen set, shown instead of the
    // document's, and writes the document once when it ends.
    std::optional<SmbScreenSet> stroke_;
    bool strokeChanged_ = false;

    mutable QVector<sdk::LibraryItem> library_;
    mutable QString libraryKey_;
};

}  // namespace rl::smb
