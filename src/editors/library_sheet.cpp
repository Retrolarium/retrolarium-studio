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

void LibrarySheet::setItems(const QVector<sdk::LibraryItem>& items, const QString& current) {
    const bool resized = items.size() != items_.size();
    items_ = items;
    current_ = current;
    if (resized) {
        updateGeometry();
    }
    update();
}

int LibrarySheet::cellSize() const {
    return std::max(1, width() / kColumns);
}

int LibrarySheet::heightForWidth(int width) const {
    const int rows = (int(items_.size()) + kColumns - 1) / kColumns;
    return rows * std::max(1, width / kColumns);
}

QSize LibrarySheet::sizeHint() const {
    return {kColumns * 16, heightForWidth(kColumns * 16)};
}

int LibrarySheet::indexAt(QPoint pos) const {
    const int cell = cellSize();
    const int column = pos.x() / cell;
    const int index = (pos.y() / cell) * kColumns + column;
    return pos.x() >= 0 && pos.y() >= 0 && column < kColumns && index < items_.size() ? index : -1;
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
    if (!(event->buttons() & Qt::LeftButton) || pressed_ < 0 ||
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
        const int size = std::max(cellSize(), 32);
        drag->setPixmap(QPixmap::fromImage(item.icon.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation)));
        drag->setHotSpot(QPoint(size / 2, size / 2));
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
    const int cell = cellSize();
    painter.fillRect(QRect(0, 0, cell * kColumns, heightForWidth(width())), t.canvas);
    for (int i = 0; i < items_.size(); ++i) {
        const QRect r((i % kColumns) * cell, (i / kColumns) * cell, cell, cell);
        const QImage& icon = items_.at(i).icon;
        if (!icon.isNull()) {
            // Pixel art at the cell size, crisp.
            painter.drawImage(r.adjusted(1, 1, -1, -1), icon);
        }
    }
    // A faint grid every tile, stronger every fourth like a pattern viewer.
    for (int c = 1; c < kColumns; ++c) {
        painter.fillRect(QRect(c * cell, 0, 1, heightForWidth(width())), c % 4 == 0 ? t.lineStrong : t.line);
    }
    const int rows = (int(items_.size()) + kColumns - 1) / kColumns;
    for (int r = 1; r < rows; ++r) {
        painter.fillRect(QRect(0, r * cell, cell * kColumns, 1), r % 4 == 0 ? t.lineStrong : t.line);
    }
    for (int i = 0; i < items_.size(); ++i) {
        const QRect r((i % kColumns) * cell, (i / kColumns) * cell, cell + 1, cell + 1);
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
    header->addWidget(new ui::Label(QStringLiteral("Patterns"), ui::Label::Role::Caps, this));
    header->addStretch(1);
    hint_ = new ui::Label(QString(), ui::Label::Role::Meta, this);
    header->addWidget(hint_);
    layout->addLayout(header);
    layout->addWidget(sheet_);
    layout->addStretch(1);

    QPointer<sdk::EditorModel> guarded = model_;
    sheet_->onPick = [guarded](const QString& id) {
        if (guarded) {
            guarded->setCurrentLibraryItem(id);
        }
    };
    // Strokes change the model on every move: refresh once per event loop.
    refresh_->setSingleShot(true);
    refresh_->setInterval(0);
    connect(refresh_, &QTimer::timeout, this, &SheetPane::refresh);
    for (auto signal : {&sdk::EditorModel::contentChanged, &sdk::EditorModel::selectionChanged,
                        &sdk::EditorModel::toolsChanged}) {
        connect(model_, signal, refresh_, qOverload<>(&QTimer::start));
    }
    refresh();
}

void SheetPane::refresh() {
    const sdk::Library library = model_->sheet();
    hint_->setText(library.hint);
    sheet_->setItems(library.items, model_->currentLibraryItem());
    setVisible(!library.items.isEmpty());
}

}  // namespace rl::editors
