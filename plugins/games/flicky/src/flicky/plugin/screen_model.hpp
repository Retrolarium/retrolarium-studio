#pragma once

#include "flicky/render/scene_renderer.hpp"
#include "sdk/editor_model.hpp"

namespace rl::flicky {

class FlickySession;

// A front-end screen as the game draws it (title, guide, ending or the
// credits roll), with its text picked on the screen and edited in the
// inspector (the strings live in the game text).
class ScreenModel final : public sdk::TileMapModel {
    Q_OBJECT

public:
    using Screen = FlickyLevelSceneRenderer::Screen;

    ScreenModel(FlickySession* session, Screen screen);

    QString title() const override { return screenTitle(screen_); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QSize gridSize() const override;
    QSize cellSize() const override { return {8, 8}; }
    bool scrollsDown() const override { return screen_ == Screen::Credits; }
    QVector<sdk::Tool> tools() const override;
    QImage render() const override;
    QVector<sdk::MapObject> objects() const override;
    QString selectedObject() const override { return selected_; }
    void selectObject(const QString& id) override;

    // The document ids of the screens, in list order, and their titles.
    static QStringList documentIds();
    static bool screenOf(const QString& document, Screen* screen);
    static QString screenTitle(Screen screen);
    // The semantics texts a screen draws.
    static QStringList textIds(const FlickySession* session, Screen screen);

private:
    FlickySession* session_;
    Screen screen_;
    QString selected_;
};

}  // namespace rl::flicky
