#pragma once

#include "sdk/editor_model.hpp"
#include "smb/render/level_renderer.hpp"

namespace rl::smb {

class SmbSession;

// One SMB area on the map editor: the rendered level (13 rows of 16px
// metatiles, pages of 16 columns), its area objects and enemies as map
// objects, and Hero, whose cell is where "Play from selected" starts.
class SmbLevelModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    SmbLevelModel(SmbSession* session, const QString& area);

    QString title() const override;
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString startPoint() const override;

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

private:
    const SmbLevelScene& scene() const;

    SmbSession* session_;
    QString area_;
    QString selected_;
    QPoint dragOffset_;   // Hero's feet cell relative to the dragged top-left cell
    mutable SmbLevelScene scene_;
    mutable quint64 sceneRevision_ = ~quint64(0);
};

}  // namespace rl::smb
