#pragma once

#include "editors/editor_view.hpp"

#include <QImage>
#include <QPoint>

class QScrollArea;

namespace rl::sdk {
class TileModel;
}

namespace rl::editors {

class TileAtlas;
class PixelCanvas;
class PenRow;

// Universal tile editor: the bank atlas on the left, the selected 8x8 tile
// on a large pixel grid with its pens on the right.
class TileEditor final : public EditorView {
    Q_OBJECT

public:
    explicit TileEditor(sdk::TileModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;
    QString currentTool() const { return tool_; }

private:
    void chooseTool(const QString& id);

    sdk::TileModel* tiles_;
    TileAtlas* atlas_;
    PixelCanvas* canvas_;
    PenRow* pens_;
    QString tool_ = QStringLiteral("pencil");
};

// Every tile of the bank, 16 per row; tall tiles take tall cells.
class TileAtlas final : public QWidget {
    Q_OBJECT

public:
    explicit TileAtlas(sdk::TileModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    void refresh();
    int cellHeight() const;

    static constexpr int kColumns = 16;
    static constexpr int kCell = 34;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    sdk::TileModel* model_;
    void renderNow();

    QImage image_;  // all tiles at 1x, 16 per row
    // A stroke changes the bank once per pixel: rebuild once after them.
    bool renderPending_ = false;
};

// The selected tile as a grid of large pixels.
class PixelCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit PixelCanvas(sdk::TileModel* model, QWidget* parent = nullptr);

    void setTool(const QString& tool) { tool_ = tool; }
    QSize sizeHint() const override;

    static constexpr int kPixel = 40;

signals:
    void actionFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect gridRect() const;
    QPoint pixelAt(const QPoint& pos) const;
    void paintLine(QPoint from, QPoint to);

    sdk::TileModel* model_;
    int pixel_ = kPixel;    // smaller for tall tiles
    QString tool_ = QStringLiteral("pencil");
    QPoint hover_{-1, -1};
    bool painting_ = false;
    int pen_ = 0;
    QPoint last_;
};

// The pens of the bank, labelled with their index.
class PenRow final : public QWidget {
    Q_OBJECT

public:
    explicit PenRow(sdk::TileModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override;

    static constexpr int kWidth = 329;
    static constexpr int kHeight = 24;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect penRect(int pen) const;

    sdk::TileModel* model_;
};

}  // namespace rl::editors
