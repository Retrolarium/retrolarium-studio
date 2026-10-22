#pragma once

#include "editors/editor_view.hpp"

#include <QAbstractScrollArea>
#include <QTimer>

class QLineEdit;

namespace rl::sdk {
class HexModel;
}

namespace rl::editors {

class HexView;

// Universal hex editor: offsets, sixteen bytes and their text a row, a
// cursor, a selection to copy or fill, and Go to. Bytes are overwritten,
// never inserted or cut, so nothing after them moves.
class HexEditor final : public EditorView {
    Q_OBJECT

public:
    explicit HexEditor(sdk::HexModel* model, QWidget* parent = nullptr);

    QWidget* createToolbar(QWidget* parent) override;

private:
    void goTo();
    void fill();

    sdk::HexModel* hex_;
    HexView* view_;
    QLineEdit* goField_ = nullptr;
    QLineEdit* fillField_ = nullptr;
};

// The rows, painted as they scroll into view. Click a byte to put the cursor
// there, drag or Shift+click to select (past the top or the bottom the rows
// scroll), click an offset to select its row or drag from it;
// type two hex digits to overwrite the byte under the cursor. The context
// menu starts a selection that clicks and scrolling keep, and ends it.
class HexView final : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit HexView(sdk::HexModel* model, QWidget* parent = nullptr);

    void copySelection();
    void showCursor();

signals:
    void goToAsked();
    void fillAsked();
    void actionFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Hit {
        int offset = -1;
        bool row = false;  // on the offset column: the whole row
    };
    Hit hitAt(const QPoint& pos) const;
    int rowCount() const;
    int visibleRows() const;
    void updateScroll();
    void moveCursor(int offset, bool extend);
    void typeDigit(int digit);
    void showMenu(const QPoint& at, int offset);
    // The byte a drag at `pos` reaches (-1: none).
    int dragTarget(const QPoint& pos) const;
    void dragTo(const QPoint& pos);
    void scrollWhileDragging();

    sdk::HexModel* model_;
    QFont font_;
    int charWidth_ = 8;
    int rowHeight_ = 20;
    int anchor_ = 0;           // where a drag or Shift selection starts
    int placed_ = 0;           // the cursor as the view last saw it
    bool selfMoving_ = false;  // the view moves the cursor itself: it scrolls nothing
    bool dragging_ = false;
    bool dragMoved_ = false;
    bool rowPress_ = false;  // the drag began on a row number
    QPoint dragPos_;
    QTimer scrollTimer_;     // scrolls while a drag is past the top or the bottom
    bool marking_ = false;   // a selection started from the menu, kept until ended
    int pendingDigit_ = -1;  // the high nibble typed, waiting for the low one
};

}  // namespace rl::editors
