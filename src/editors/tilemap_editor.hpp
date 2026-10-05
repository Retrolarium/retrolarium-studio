#pragma once

#include "editors/editor_view.hpp"

#include <QImage>
#include <QPoint>
#include <QString>

class QScrollArea;

namespace rl::sdk {
class TileMapModel;
}

namespace rl::editors {

class MapCanvas;

// Universal tile-map editor: the rendered map on the canvas ground with its
// cell overlay, grid and objects, and the tool / layer / view toolbar.
class TileMapEditor final : public EditorView {
    Q_OBJECT

public:
    explicit TileMapEditor(sdk::TileMapModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

    // Zoom in quarters: 1 is a quarter size, 4 one to one, 32 eight times.
    int zoom() const;
    void setZoom(int zoom);
    bool gridVisible() const;
    void setGridVisible(bool visible);
    QString currentTool() const { return tool_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    void chooseTool(const QString& id);
    void fitZoom();

    sdk::TileMapModel* map_;
    QScrollArea* scroll_;
    MapCanvas* canvas_;
    QString tool_;
    bool fitted_ = false;
};

// The drawing surface of TileMapEditor.
class MapCanvas final : public QWidget {
    Q_OBJECT

public:
    MapCanvas(sdk::TileMapModel* model, QWidget* parent = nullptr);

    int zoom() const { return zoom_; }
    void setZoom(int zoom);
    bool gridVisible() const { return grid_; }
    void setGridVisible(bool visible);
    void setTool(const QString& tool);
    void refresh();

    QSize sizeHint() const override;
    QRect mapRect() const;

signals:
    void zoomRequested(int delta);
    void actionFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QPoint cellAt(const QPoint& pos) const;
    bool insideMap(QPoint cell) const;
    QString objectAt(QPoint cell) const;
    void paintLine(QPoint from, QPoint to, int value);
    void updateCursor();

    sdk::TileMapModel* model_;
    QImage image_;
    int scaled(int pixels) const { return pixels * zoom_ / 4; }

    int zoom_ = 8;      // quarters
    bool grid_ = true;
    QString tool_ = QStringLiteral("select");

    QPoint hoverCell_{-1, -1};
    bool painting_ = false;
    int paintValue_ = 0;
    QPoint lastPaintCell_;
    QString dragging_;
    QPoint dragOffset_;
};

}  // namespace rl::editors
