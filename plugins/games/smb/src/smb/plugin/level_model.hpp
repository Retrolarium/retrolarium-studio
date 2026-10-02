#pragma once

#include "sdk/editor_model.hpp"
#include "smb/data/resource_document.hpp"
#include "smb/render/level_renderer.hpp"

#include <optional>

namespace rl::smb {

class SmbSession;

// One SMB area on the map editor: the rendered level (13 rows of 16px
// metatiles, pages of 16 columns), its area objects and enemies as map
// objects, and Hero, whose cell is where "Play from selected" starts.
//
// Objects and enemies move, are added from the library and removed through
// the stream helpers, so every other record keeps its page and column. An
// edit that would not fit the bytes the ROM leaves for the area is refused.
class SmbLevelModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SmbLevelModel(SmbSession* session, const QString& area);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString startPoint() const override;
    QString currentLibraryItem() const override { return libraryItem_; }
    void setCurrentLibraryItem(const QString& id) override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {16, 16}; }
    int pageColumns() const override { return 16; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;
    bool placeObject(const QString& libraryItem, QPoint cell, QString* error) override;
    bool removeObject(const QString& id, QString* error) override;

private:
    const SmbLevelScene& scene() const;
    SmbArea area() const;
    // Writes an edited area as one undo step, if its streams fit.
    bool commitArea(const SmbArea& area, const QString& text, QString* error);
    // The area with one record moved to an absolute column and a row; the
    // record's new index comes back in *index.
    std::optional<SmbArea> moved(const SmbArea& area, const QString& id, int column, int row, int* index) const;
    sdk::Library library() const;
    QImage libraryIcon(const QString& id) const;
    // The selected record as it stands; empty for Hero or no selection.
    QVariantList selectedRecord() const;
    // Undo, redo and other editors renumber records: drop a selection whose
    // index now names a different record.
    void checkSelection();

    SmbSession* session_;
    QString area_;
    QString selected_;
    QVariantList selectedRecord_;
    bool editing_ = false;  // this model is writing the document
    QString libraryItem_;

    // A drag in progress: the document it started from.
    std::optional<SmbResourceDocument> before_;
    quint64 beforeRevision_ = 0;
    QString dragging_;
    QPoint dragOffset_;   // Hero's feet relative to the dragged top-left cell
    bool dragChanged_ = false;

    mutable SmbLevelScene scene_;
    mutable quint64 sceneRevision_ = ~quint64(0);
    mutable QVector<sdk::LibraryItem> library_;
    mutable quint64 libraryRevision_ = ~quint64(0);
};

}  // namespace rl::smb
