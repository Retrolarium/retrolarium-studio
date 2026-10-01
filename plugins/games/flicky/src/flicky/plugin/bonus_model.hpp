#pragma once

#include "flicky/data/level_document.hpp"
#include "sdk/editor_model.hpp"

#include <optional>

namespace rl::flicky {

class FlickySession;

// A bonus round in the tile-map editor. Bonus rounds draw no layout: the
// scene is simulated frame by frame from the bonus tables, and the seesaws,
// tigers and chick spawn are placed by bonus_placements, which every bonus
// round shares. X has a value per side; Y is shared by both sides.
class BonusRoundModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    BonusRoundModel(FlickySession* session, int round);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QVector<sdk::ViewOption> viewOptions() const override;
    void setViewOption(const QString& key, const QString& value) override;
    QString statusText() const override;
    QString startPoint() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;

    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;
    void beginDrag(const QString& id) override;
    bool dragObject(const QString& id, QPoint topLeftCell) override;
    void endDrag() override;

private:
    struct Actor {
        QString placement;  // seesaw, tiger, chick_spawn
        int side = 0;
        bool isValid() const { return !placement.isEmpty(); }
    };
    static Actor parseId(const QString& id);
    // Visible sprite bounds relative to the anchor, in pixels.
    QRect bounds(const Actor& actor) const;
    QPoint anchor(const Actor& actor) const;  // screen pixels
    bool place(const Actor& actor, int worldX, int worldY, QString* error, bool live);

    FlickySession* session_;
    int round_;
    QString selected_;
    std::optional<FlickyLevelDocument> before_;
    QPoint dragOffset_;
};

}  // namespace rl::flicky
