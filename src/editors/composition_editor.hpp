#pragma once

#include "editors/editor_view.hpp"

#include <QImage>
#include <QPoint>

class QHBoxLayout;
class QTimer;
class QScrollArea;

namespace rl::sdk {
class CompositionModel;
}

namespace rl::editors {

class CompositionCanvas;

// Universal composition editor: pieces on a canvas (sprite mappings,
// metatiles) and, for animations, a strip of frames that plays.
class CompositionEditor final : public EditorView {
    Q_OBJECT

public:
    explicit CompositionEditor(sdk::CompositionModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

private:
    void rebuildFrames();
    void setPlaying(bool playing);
    void step();

    sdk::CompositionModel* composition_;
    CompositionCanvas* canvas_;
    QWidget* strip_;
    QHBoxLayout* stripLayout_;
    QTimer* timer_;
    bool playing_ = false;
};

class CompositionCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit CompositionCanvas(sdk::CompositionModel* model, QWidget* parent = nullptr);

    int zoom() const { return zoom_; }
    // A fixed zoom; turns fitting off.
    void setZoom(int zoom);
    // Fitting picks the largest whole zoom at which the focus area fits the
    // view, never below 1x (the view scrolls then).
    bool isFitting() const { return fit_; }
    void setFitting(bool fit);
    void refresh();
    QSize sizeHint() const override;

signals:
    void zoomRequested(int delta);
    void zoomChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect imageRect() const;
    QRect toScreen(const QRect& pixels) const;
    void applyFit();
    QString pieceAt(const QPoint& pos) const;

    sdk::CompositionModel* model_;
    QImage image_;
    int zoom_ = 2;
    bool fit_ = true;
    QRect view_;                // canvas pixels shown; grows, never shrinks
    QString dragging_;
    QPoint pressPos_;
};

}  // namespace rl::editors
