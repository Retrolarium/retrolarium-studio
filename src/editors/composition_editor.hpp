#pragma once

#include "editors/editor_view.hpp"

#include <QImage>
#include <QPoint>
#include <QStringList>
#include <QVector>

class QHBoxLayout;
class QTimer;
class QToolButton;
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
    QScrollArea* scroll_ = nullptr;
    CompositionCanvas* canvas_;
    QWidget* strip_;
    QHBoxLayout* stripLayout_;
    QTimer* timer_;
    bool playing_ = false;
    // The strip as built: rebuilt only when the frames change, so a frame
    // step while playing keeps the play button under the pointer.
    QStringList stripSignature_;
    QToolButton* play_ = nullptr;
    QVector<QWidget*> cards_;
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
    void zoomRequested(int delta, QPoint anchor);   // anchor: the mouse, canvas coordinates
    void zoomChanged();
    void actionFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public:
    // Where the picture is drawn in the canvas.
    QRect imageRect() const;

private:
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
