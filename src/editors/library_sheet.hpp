#pragma once

#include "sdk/types.hpp"

#include <QWidget>

#include <functional>

class QTimer;

namespace rl::sdk {
class EditorModel;
}

namespace rl::ui {
class Label;
}

namespace rl::editors {

// Library items as a sheet of square tiles, sixteen to a row like a pattern
// table viewer: a click picks one, dragging drops it onto a canvas, the
// tooltip names it. Tiles grow with the width.
class LibrarySheet final : public QWidget {
public:
    explicit LibrarySheet(QWidget* parent = nullptr);

    void setItems(const QVector<sdk::LibraryItem>& items, const QString& current);
    std::function<void(const QString&)> onPick;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kColumns = 16;

    int cellSize() const;
    int indexAt(QPoint pos) const;

    QVector<sdk::LibraryItem> items_;
    QString current_;
    int hovered_ = -1;
    int pressed_ = -1;
    QPoint pressPos_;
};

// A model's sheet (EditorModel::sheet) beside the canvas: a header and the
// sheet at a size worth clicking, following the model as it changes.
class SheetPane final : public QWidget {
    Q_OBJECT

public:
    explicit SheetPane(sdk::EditorModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {16 * 24 + 32, 480}; }

private:
    void refresh();

    sdk::EditorModel* model_;
    ui::Label* hint_;
    LibrarySheet* sheet_;
    QTimer* refresh_;
};

}  // namespace rl::editors
