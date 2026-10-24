#include "editors/library_sheet.hpp"

#include "editors/mime.hpp"
#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "widgets/label.hpp"

#include <QApplication>
#include <QDrag>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace rl::editors {

// ---------------------------------------------------------------- sheet

LibrarySheet::LibrarySheet(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void LibrarySheet::setItems(const QVector<sdk::LibraryItem>& items, const QString& current, int columns, QSize cell) {
    columns = std::max(1, columns);
    if (cell.isEmpty()) cell = QSize(1, 1);
    const bool resized = items.size() != items_.size() || columns != columns_ || cell != cell_;
    items_ = items;
    current_ = current;
    columns_ = columns;
    cell_ = cell;
    if (resized) {
        updateGeometry();
    }
    update();
}

int LibrarySheet::cellWidth() const { return std::max(1, width() / columns_); }

int LibrarySheet::cellHeight(int width) const { return std::max(1, width * cell_.height() / cell_.width()); }

int LibrarySheet::heightForWidth(int width) const {
    const int rows = (int(items_.size()) + columns_ - 1) / columns_;
    return rows * cellHeight(std::max(1, width / columns_));
}

QSize LibrarySheet::sizeHint() const {
    // Cells at least twice their pixel size, square ones at least 16.
    const int cell = std::max(16, 2 * cell_.width());
    return {columns_ * cell, heightForWidth(columns_ * cell)};
}

int LibrarySheet::indexAt(QPoint pos) const {
    const int w = cellWidth();
    const int column = pos.x() / w;
    const int index = (pos.y() / cellHeight(w)) * columns_ + column;
    return pos.x() >= 0 && pos.y() >= 0 && column < columns_ && index < items_.size() ? index : -1;
}

bool LibrarySheet::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = indexAt(help->pos());
        if (index >= 0) {
            const sdk::LibraryItem& item = items_.at(index);
            QToolTip::showText(help->globalPos(),
                               item.hint.isEmpty() ? item.label : item.label + QStringLiteral(" · ") + item.hint, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

void LibrarySheet::leaveEvent(QEvent*) {
    hovered_ = -1;
    update();
}

void LibrarySheet::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressPos_ = event->pos();
        pressed_ = indexAt(event->pos());
    }
}

void LibrarySheet::mouseMoveEvent(QMouseEvent* event) {
    const int index = indexAt(event->pos());
    if (index != hovered_) {
        hovered_ = index;
        update();
    }
    // The items can change between press and drag (the pane refreshes).
    if (!(event->buttons() & Qt::LeftButton) || pressed_ < 0 || pressed_ >= items_.size() ||
        (event->pos() - pressPos_).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    const sdk::LibraryItem item = items_.at(pressed_);
    pressed_ = -1;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kLibraryItemMime), item.id.toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    if (!item.icon.isNull()) {
        const int size = std::max(std::max(cellWidth(), cellHeight(cellWidth())), 32);
        const QPixmap pixmap =
            QPixmap::fromImage(item.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation));
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    }
    drag->exec(Qt::CopyAction);
}

void LibrarySheet::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pressed_ >= 0 && pressed_ == indexAt(event->pos())) {
        current_ = items_.at(pressed_).id;
        update();
        if (onPick) {
            onPick(current_);
        }
    }
    pressed_ = -1;
}

void LibrarySheet::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const int cw = cellWidth();
    const int ch = cellHeight(cw);
    const int height = heightForWidth(width());
    painter.fillRect(QRect(0, 0, cw * columns_, height), t.canvas);
    for (int i = 0; i < items_.size(); ++i) {
        const QRect r((i % columns_) * cw, (i / columns_) * ch, cw, ch);
        const QImage& icon = items_.at(i).icon;
        if (!icon.isNull()) {
            // Pixel art at the cell size, crisp.
            painter.drawImage(r.adjusted(1, 1, -1, -1), icon);
        }
    }
    // A faint grid every tile, stronger every fourth like a pattern viewer.
    for (int c = 1; c < columns_; ++c) {
        painter.fillRect(QRect(c * cw, 0, 1, height), c % 4 == 0 ? t.lineStrong : t.line);
    }
    const int rows = (int(items_.size()) + columns_ - 1) / columns_;
    for (int r = 1; r < rows; ++r) {
        painter.fillRect(QRect(0, r * ch, cw * columns_, 1), r % 4 == 0 ? t.lineStrong : t.line);
    }
    for (int i = 0; i < items_.size(); ++i) {
        const QRect r((i % columns_) * cw, (i / columns_) * ch, cw + 1, ch + 1);
        if (items_.at(i).id == current_) {
            theme::paint::hairlineRect(painter, r, t.accent, 2.0);
        } else if (i == hovered_) {
            theme::paint::hairlineRect(painter, r, t.text);
        }
    }
}

// ---------------------------------------------------------------- pane

SheetPane::SheetPane(sdk::EditorModel* model, QWidget* parent)
    : QWidget(parent), model_(model), sheet_(new LibrarySheet(this)), refresh_(new QTimer(this)) {
    setProperty("surface", QStringLiteral("panel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    auto* header = new QHBoxLayout;
    header->addWidget(new ui::Label(tr("Patterns"), ui::Label::Role::Caps, this));
    header->addStretch(1);
    hint_ = new ui::Label(QString(), ui::Label::Role::Meta, this);
    header->addWidget(hint_);
    layout->addLayout(header);
    layout->addWidget(sheet_);
    layout->addStretch(1);

    QPointer<sdk::EditorModel> guarded = model_;
    sheet_->onPick = [this, guarded](const QString& id) {
        if (guarded) {
            guarded->setCurrentLibraryItem(id);
            emit picked(id);
        }
    };
    // Strokes change the model on every move: refresh once per event loop.
    refresh_->setSingleShot(true);
    refresh_->setInterval(0);
    connect(refresh_, &QTimer::timeout, this, &SheetPane::refresh);
    for (auto signal :
         {&sdk::EditorModel::contentChanged, &sdk::EditorModel::selectionChanged, &sdk::EditorModel::toolsChanged}) {
        connect(model_, signal, refresh_, qOverload<>(&QTimer::start));
    }
    refresh();
}

QSize SheetPane::sizeHint() const { return {std::max(16 * 24, sheet_->sizeHint().width()) + 32, 480}; }

void SheetPane::refresh() {
    const sdk::Library library = model_->sheet();
    hint_->setText(library.hint);
    sheet_->setItems(library.items, model_->currentLibraryItem(), library.columns, library.cell);
    setVisible(!library.items.isEmpty());
}

}  // namespace rl::editors
