#include "editors/tile_editor.hpp"

#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/segmented_control.hpp"
#include "widgets/view_chip.hpp"

#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>

namespace rl::editors {

namespace paint = theme::paint;

namespace {

// Readable label color over a swatch.
QColor inkOver(const QColor& color) {
    return color.lightness() > 150 ? QColor(0, 0, 0, 200) : QColor(255, 255, 255, 230);
}

// The tool, and the pen per kind of tile bank, last chosen: opening another
// bank keeps drawing with them.
QHash<QString, int>& lastPens() {
    static QHash<QString, int> pens;
    return pens;
}
QString& lastTileTool() {
    static QString tool;
    return tool;
}

}  // namespace

// ---------------------------------------------------------------- atlas

TileAtlas::TileAtlas(sdk::TileModel* model, QWidget* parent) : QWidget(parent), model_(model) {
    setFocusPolicy(Qt::ClickFocus);
    setCursor(Qt::PointingHandCursor);
    connect(model_, &sdk::EditorModel::contentChanged, this, &TileAtlas::refresh);
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, &TileAtlas::refresh);
    setFixedSize(sizeHint());
}

void TileAtlas::refresh() {
    if (refreshPending_) {
        return;
    }
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshPending_ = false;
        setFixedSize(sizeHint());
        update();
    });
}

int TileAtlas::cellHeight() const {
    const QSize size = model_->tileSize();
    return kCell * size.height() / std::max(1, size.width());
}

QSize TileAtlas::sizeHint() const {
    const int rows = std::max(1, (model_->tileCount() + kColumns - 1) / kColumns);
    return QSize(kColumns * kCell, rows * cellHeight());
}

void TileAtlas::paintEvent(QPaintEvent* event) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    painter.fillRect(rect(), t.canvas);
    const int count = model_->tileCount();
    const QRect dirty = event->rect();
    const QSize size = model_->tileSize();
    const int height = cellHeight();
    QVector<QRgb> colors;
    for (const QColor& color : model_->penColors()) colors.append(color.rgb());
    QImage image(size, QImage::Format_RGB32);
    // Only the rows in view: a ROM's worth of tiles is too many to draw.
    const int firstRow = std::max(0, dirty.top() / height);
    const int lastRow = dirty.bottom() / height;
    for (int tile = firstRow * kColumns; tile < count && tile < (lastRow + 1) * kColumns; ++tile) {
        const QRect cell((tile % kColumns) * kCell, (tile / kColumns) * height, kCell, height);
        if (!cell.intersects(dirty)) {
            continue;
        }
        const QVector<quint8> pixels = model_->tilePixels(tile);
        for (int i = 0; i < size.width() * size.height(); ++i) {
            const int pen = i < pixels.size() ? pixels.at(i) : 0;
            image.setPixel(i % size.width(), i / size.width(), pen < colors.size() ? colors.at(pen) : qRgb(0, 0, 0));
        }
        painter.drawImage(cell.adjusted(1, 1, -1, -1), image);
        if (model_->isTileDirty(tile)) {
            painter.fillRect(QRect(cell.left() + 2, cell.top() + 2, 4, 4), t.accent);
        }
    }
    const int current = model_->currentTile();
    if (current >= 0 && current < count) {
        const QRect cell((current % kColumns) * kCell, (current / kColumns) * height, kCell, height);
        paint::hairlineRect(painter, cell, theme::canvas::kSelection, 2.0);
    }
}

void TileAtlas::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const int tile = (event->pos().y() / cellHeight()) * kColumns + event->pos().x() / kCell;
    if (event->pos().x() < kColumns * kCell && tile >= 0 && tile < model_->tileCount()) {
        model_->setCurrentTile(tile);
    }
}

void TileAtlas::keyPressEvent(QKeyEvent* event) {
    // Ctrl+Left and Ctrl+Right move where the tiles start by a byte, for
    // graphics that do not start on a tile boundary.
    if ((event->modifiers() & Qt::ControlModifier) && model_->canShiftStart() &&
        (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        model_->shiftStart(event->key() == Qt::Key_Left ? -1 : 1);
        return;
    }
    int step = 0;
    switch (event->key()) {
    case Qt::Key_Left: step = -1; break;
    case Qt::Key_Right: step = 1; break;
    case Qt::Key_Up: step = -kColumns; break;
    case Qt::Key_Down: step = kColumns; break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    if (model_->tileCount() <= 0) {
        return;
    }
    const int tile = std::clamp(model_->currentTile() + step, 0, model_->tileCount() - 1);
    model_->setCurrentTile(tile);
}

// ---------------------------------------------------------------- pixels

PixelCanvas::PixelCanvas(sdk::TileModel* model, QWidget* parent) : QWidget(parent), model_(model) {
    // A tall tile at the 8x8 scale would not fit the window.
    pixel_ = model_->tileSize().height() > 8 ? 26 : kPixel;
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFixedSize(sizeHint());
    connect(model_, &sdk::EditorModel::contentChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, qOverload<>(&QWidget::update));
}

QSize PixelCanvas::sizeHint() const {
    // The pixels with 1px gaps, a 1px frame and room for the corner marks.
    return gridRect().size() + QSize(14, 14);
}

QRect PixelCanvas::gridRect() const {
    const QSize size = model_->tileSize();
    return QRect(7, 7, size.width() * pixel_ + size.width() - 1 + 2, size.height() * pixel_ + size.height() - 1 + 2);
}

QPoint PixelCanvas::pixelAt(const QPoint& pos) const {
    const QPoint local = pos - gridRect().topLeft() - QPoint(1, 1);
    if (local.x() < 0 || local.y() < 0) {
        return {-1, -1};
    }
    const QSize size = model_->tileSize();
    const QPoint p(local.x() / (pixel_ + 1), local.y() / (pixel_ + 1));
    return p.x() < size.width() && p.y() < size.height() ? p : QPoint(-1, -1);
}

void PixelCanvas::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    const QRect grid = gridRect();
    painter.fillRect(grid, QColor(0, 0, 0, 90));
    paint::hairlineRect(painter, grid, t.lineStrong);
    const QVector<quint8> pixels = model_->tilePixels(model_->currentTile());
    const QVector<QColor> colors = model_->penColors();
    const QSize size = model_->tileSize();
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const int index = y * size.width() + x;
            const int pen = index < pixels.size() ? pixels.at(index) : 0;
            const QRect cell(grid.left() + 1 + x * (pixel_ + 1), grid.top() + 1 + y * (pixel_ + 1), pixel_, pixel_);
            painter.fillRect(cell, pen < colors.size() ? colors.at(pen) : QColor(Qt::black));
            if (QPoint(x, y) == hover_) {
                paint::hairlineRect(painter, cell, QColor(255, 255, 255, 220));
            }
        }
    }
    paint::cornerMarks(painter, grid, 11, 6, t.mark);
}

void PixelCanvas::paintLine(QPoint from, QPoint to) {
    int x0 = from.x();
    int y0 = from.y();
    const int dx = std::abs(to.x() - x0);
    const int dy = -std::abs(to.y() - y0);
    const int sx = x0 < to.x() ? 1 : -1;
    const int sy = y0 < to.y() ? 1 : -1;
    int error = dx + dy;
    while (true) {
        model_->setPixel(QPoint(x0, y0), pen_);
        if (x0 == to.x() && y0 == to.y()) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void PixelCanvas::mousePressEvent(QMouseEvent* event) {
    const QPoint p = pixelAt(event->pos());
    // A second button during a stroke would end it early.
    if (p.x() < 0 || painting_) {
        return;
    }
    const QVector<quint8> pixels = model_->tilePixels(model_->currentTile());
    const int under = pixels.value(p.y() * model_->tileSize().width() + p.x());
    // Right button picks the pen under the cursor whatever the tool.
    if (event->button() == Qt::RightButton || tool_ == QLatin1String("pick")) {
        model_->setCurrentPen(under);
        return;
    }
    if (event->button() != Qt::LeftButton || model_->isReadOnly()) {
        return;
    }
    if (tool_ == QLatin1String("fill")) {
        QString error;
        if (!model_->fill(p, model_->currentPen(), &error) && !error.isEmpty()) {
            emit actionFailed(error);
        }
        return;
    }
    painting_ = true;
    pen_ = model_->currentPen();
    last_ = p;
    model_->beginStroke();
    model_->setPixel(p, pen_);
}

void PixelCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPoint p = pixelAt(event->pos());
    if (p != hover_) {
        hover_ = p;
        update();
    }
    if (painting_ && p.x() >= 0 && p != last_) {
        paintLine(last_, p);
        last_ = p;
    }
}

void PixelCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (painting_ && event->button() == Qt::LeftButton) {
        painting_ = false;
        model_->endStroke();
    }
}

void PixelCanvas::leaveEvent(QEvent*) {
    hover_ = QPoint(-1, -1);
    update();
}

// ---------------------------------------------------------------- pens

PenRow::PenRow(sdk::TileModel* model, QWidget* parent) : QWidget(parent), model_(model) {
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::contentChanged, this, qOverload<>(&QWidget::update));
    // Another tile depth brings another number of pens.
    connect(model_, &sdk::EditorModel::toolsChanged, this, [this] { setFixedSize(sizeHint()); });
}

int PenRow::rowHeight() const {
    return model_->penCount() > kColumns ? kShortHeight : kHeight;
}

QSize PenRow::sizeHint() const {
    const int rows = (std::max(1, model_->penCount()) + kColumns - 1) / kColumns;
    return QSize(kWidth + 6, rows * rowHeight() + (rows - 1) * 3 + 6);
}

QRect PenRow::penRect(int pen) const {
    const int columns = std::clamp(model_->penCount(), 1, int(kColumns));
    const int gap = 3;
    const double width = (kWidth - gap * (columns - 1)) / static_cast<double>(columns);
    const int column = pen % columns;
    const int left = static_cast<int>(column * (width + gap));
    const int right = static_cast<int>((column + 1) * (width + gap) - gap);
    return QRect(3 + left, 3 + (pen / columns) * (rowHeight() + gap), right - left, rowHeight());
}

void PenRow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setFont(theme::font(theme::TextRole::Mono, model_->penCount() > kColumns ? 8.0 : 10.0));
    const QVector<QColor> colors = model_->penColors();
    for (int pen = 0; pen < model_->penCount(); ++pen) {
        const QRect r = penRect(pen);
        const QColor color = pen < colors.size() ? colors.at(pen) : QColor(Qt::black);
        painter.fillRect(r, color);
        painter.setPen(inkOver(color));
        painter.drawText(r, Qt::AlignCenter, QString::number(pen, 16).toUpper());
        if (pen == model_->currentPen()) {
            paint::hairlineRect(painter, r.adjusted(-3, -3, 3, 3), theme::current().accent, 2.0);
        }
    }
}

void PenRow::mousePressEvent(QMouseEvent* event) {
    for (int pen = 0; pen < model_->penCount(); ++pen) {
        if (penRect(pen).adjusted(-1, -3, 1, 3).contains(event->pos())) {
            model_->setCurrentPen(pen);
            return;
        }
    }
}

// ---------------------------------------------------------------- editor

TileEditor::TileEditor(sdk::TileModel* model, QWidget* parent)
    : EditorView(model, parent), tiles_(model) {
    const QString penKey = QString::fromLatin1(tiles_->metaObject()->className());
    const auto pen = lastPens().constFind(penKey);
    if (pen != lastPens().constEnd() && *pen < tiles_->penCount()) tiles_->setCurrentPen(*pen);
    connect(tiles_, &sdk::EditorModel::selectionChanged, this,
            [this, penKey] { lastPens().insert(penKey, tiles_->currentPen()); });
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(32);

    auto* atlasFrame = new QFrame(this);
    atlasFrame->setProperty("surface", QStringLiteral("canvas"));
    auto* atlasLayout = new QVBoxLayout(atlasFrame);
    atlasLayout->setContentsMargins(8, 8, 0, 8);
    auto* scroll = new QScrollArea(atlasFrame);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    atlas_ = new TileAtlas(tiles_, scroll);
    scroll->setWidget(atlas_);
    scroll->setFixedWidth(atlas_->width() + 12);
    atlasLayout->addWidget(scroll);
    layout->addWidget(atlasFrame, 0);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);
    canvas_ = new PixelCanvas(tiles_, right);
    pens_ = new PenRow(tiles_, right);
    rightLayout->addWidget(canvas_);
    rightLayout->addWidget(pens_);
    auto* hint = new ui::Label(
        tiles_->canShiftStart()
            ? tr("Right-click picks the pen under the cursor. Arrow keys move through the atlas; Ctrl+Left and "
                 "Ctrl+Right move where the tiles start by a byte.")
            : tr("Right-click picks the pen under the cursor. Arrow keys move through the atlas."),
        ui::Label::Role::Meta, right);
    hint->setWordWrap(true);
    hint->setMaximumWidth(PenRow::kWidth);
    rightLayout->addWidget(hint);
    rightLayout->addStretch(1);
    layout->addWidget(right, 0);
    layout->addStretch(1);

    const QVector<sdk::Tool> tools = tiles_->tools();
    if (std::any_of(tools.begin(), tools.end(), [](const sdk::Tool& tool) { return tool.id == lastTileTool(); })) {
        tool_ = lastTileTool();
        canvas_->setTool(tool_);
    }

    connect(canvas_, &PixelCanvas::actionFailed, this, &EditorView::actionFailed);
    connect(tiles_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    scroll_ = scroll;
    connect(tiles_, &sdk::EditorModel::selectionChanged, this, &TileEditor::showCurrentTile);
    // A model may open on a tile far down (a ROM's graphics): once laid out.
    QTimer::singleShot(0, this, &TileEditor::showCurrentTile);
}

void TileEditor::showCurrentTile() {
    // A step from the keyboard scrolls a row; a tile further away (a jump
    // to a section) comes in as the top row.
    const int height = atlas_->cellHeight();
    const int y = (tiles_->currentTile() / TileAtlas::kColumns) * height;
    QScrollBar* bar = scroll_->verticalScrollBar();
    const int top = bar->value();
    const int bottom = top + scroll_->viewport()->height();
    if (y >= top && y + height <= bottom) {
        return;
    }
    if (y + height > top - height && y < bottom + height) {
        scroll_->ensureVisible(0, y + height / 2, 0, height);
        return;
    }
    atlas_->setFixedSize(atlas_->sizeHint());
    bar->setValue(y);
}

void TileEditor::chooseTool(const QString& id) {
    tool_ = id;
    lastTileTool() = id;
    canvas_->setTool(id);
}

QWidget* TileEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* segment = new ui::SegmentedControl(bar);
    for (const sdk::Tool& tool : tiles_->tools()) {
        segment->addIconSegment(tool.icon, tool.label, tool.id);
    }
    segment->setCurrentId(tool_);
    connect(segment, &ui::SegmentedControl::currentChanged, this,
            [this](int, const QString& id) { chooseTool(id); });
    layout->addWidget(segment);
    auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, bar);
    separator->setFixedHeight(18);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
    const QVector<sdk::ViewOption> options = tiles_->viewOptions();
    if (!options.isEmpty()) {
        layout->addWidget(new ui::Label(tr("View"), ui::Label::Role::Caps, bar));
    }
    for (const sdk::ViewOption& option : options) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { tiles_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    layout->addStretch(1);
    return bar;
}

}  // namespace rl::editors
