#include "editors/tilemap_editor.hpp"

#include "editors/library_sheet.hpp"
#include "editors/mime.hpp"
#include "editors/zoom_anchor.hpp"
#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/segmented_control.hpp"
#include "widgets/view_chip.hpp"

#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace rl::editors {

namespace paint = theme::paint;

namespace {
constexpr int kPadTop = 44;
constexpr int kPadSide = 36;
constexpr int kPadBottom = 40;
// Zoom steps in quarters: large maps (Doraemon's are 2048 pixels and more)
// open below one to one.
constexpr int kZoomSteps[] = {1, 2, 4, 8, 12, 16, 20, 24, 28, 32};
constexpr int kMinZoom = 1;
constexpr int kMaxZoom = 32;

QString zoomLabel(int quarters) {
    if (quarters == 1) return QStringLiteral("¼×");
    if (quarters == 2) return QStringLiteral("½×");
    return QStringLiteral("%1×").arg(quarters / 4);
}

int zoomStep(int quarters, int delta) {
    const int count = int(sizeof(kZoomSteps) / sizeof(kZoomSteps[0]));
    int index = 0;
    while (index + 1 < count && kZoomSteps[index] < quarters) ++index;
    return kZoomSteps[std::clamp(index + delta, 0, count - 1)];
}

// The tool and, per kind of map and layer, the brush and library item last
// chosen: switching to another level or room keeps painting or adding with
// them instead of falling back to the first tool, brush and item.
QString& lastTool() {
    static QString tool;
    return tool;
}
struct Pick {
    std::optional<int> brush;
    QString item;
};
QHash<QString, Pick>& lastPicks() {
    static QHash<QString, Pick> picks;
    return picks;
}
QString pickKey(const sdk::TileMapModel* model) {
    // A tile brush and a palette brush are different values: per layer too.
    return QString::fromLatin1(model->metaObject()->className()) + QLatin1Char('/') + model->currentLayer();
}

ui::Divider* toolbarSeparator(QWidget* parent) {
    auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, parent);
    separator->setFixedHeight(18);
    return separator;
}
}  // namespace

// ---------------------------------------------------------------- canvas

MapCanvas::MapCanvas(sdk::TileMapModel* model, QWidget* parent) : QWidget(parent), model_(model) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(model_, &sdk::EditorModel::contentChanged, this, &MapCanvas::refresh);
    connect(model_, &sdk::EditorModel::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(model_, &sdk::EditorModel::toolsChanged, this, &MapCanvas::refresh);
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, qOverload<>(&QWidget::update));
    renderNow();
}

void MapCanvas::refresh() {
    if (renderPending_) {
        return;
    }
    renderPending_ = true;
    QTimer::singleShot(0, this, [this] {
        renderPending_ = false;
        renderNow();
    });
}

void MapCanvas::renderNow() {
    image_ = model_->render();
    setMinimumSize(sizeHint());
    update();
}

QRect MapCanvas::hoverRect(QPoint cell) const {
    const QRect map = mapRect();
    const int cw = std::max(1, scaled(model_->cellSize().width()));
    const int ch = std::max(1, scaled(model_->cellSize().height()));
    // The Add tool's preview can cover more than the cell.
    const QSize size = tool_ == QLatin1String("place") && !previewIcon_.isNull() ? previewSize() : QSize(cw, ch);
    return QRect(map.left() + cell.x() * cw, map.top() + cell.y() * ch, size.width(), size.height())
        .adjusted(-3, -3, 3, 3);
}

QSize MapCanvas::previewSize() const {
    const int cw = std::max(1, scaled(model_->cellSize().width()));
    const int ch = std::max(1, scaled(model_->cellSize().height()));
    if (!previewCells_.isEmpty()) return QSize(cw * previewCells_.width(), ch * previewCells_.height());
    // No footprint given: the item at its own size (its icon is in map
    // pixels), at least a cell.
    return QSize(scaled(previewIcon_.width()), scaled(previewIcon_.height())).expandedTo(QSize(cw, ch));
}

void MapCanvas::setHoverCell(QPoint cell) {
    if (cell == hoverCell_) {
        return;
    }
    // Only the two outlines change: repainting a large zoomed map for them
    // made hovering lag.
    if (insideMap(hoverCell_)) {
        update(hoverRect(hoverCell_));
    }
    hoverCell_ = cell;
    if (insideMap(hoverCell_)) {
        update(hoverRect(hoverCell_));
    }
}

void MapCanvas::setZoom(int zoom) {
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom != zoom_) {
        zoom_ = zoom;
        // The scroll area grows the canvas to the viewport; the minimum keeps
        // the whole map reachable through its scroll bars.
        setMinimumSize(sizeHint());
        update();
    }
}

void MapCanvas::setGridVisible(bool visible) {
    grid_ = visible;
    update();
}

void MapCanvas::setTool(const QString& tool) {
    tool_ = tool;
    updateCursor();
    update();
}

void MapCanvas::setPlacePreview(const QImage& icon, QSize cells) {
    previewIcon_ = icon;
    previewCells_ = cells;
    update();
}

void MapCanvas::updateCursor() { setCursor(tool_ == QLatin1String("select") ? Qt::ArrowCursor : Qt::CrossCursor); }

QSize MapCanvas::sizeHint() const {
    const QSize map(scaled(model_->gridSize().width() * model_->cellSize().width()),
                    scaled(model_->gridSize().height() * model_->cellSize().height()));
    return QSize(map.width() + kPadSide * 2, map.height() + kPadTop + kPadBottom);
}

QRect MapCanvas::mapRect() const {
    const QSize hint = sizeHint();
    const QSize map(hint.width() - kPadSide * 2, hint.height() - kPadTop - kPadBottom);
    const int x = std::max(kPadSide, (width() - map.width()) / 2);
    const int y = std::max(kPadTop, (height() - map.height() - kPadTop - kPadBottom) / 2 + kPadTop);
    return QRect(QPoint(x, y), map);
}

QPoint MapCanvas::cellAt(const QPoint& pos) const {
    const QRect map = mapRect();
    const int cw = std::max(1, scaled(model_->cellSize().width()));
    const int ch = std::max(1, scaled(model_->cellSize().height()));
    const QPoint local = pos - map.topLeft();
    return QPoint(local.x() >= 0 ? local.x() / cw : -1, local.y() >= 0 ? local.y() / ch : -1);
}

bool MapCanvas::insideMap(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < model_->gridSize().width() &&
           cell.y() < model_->gridSize().height();
}

QString MapCanvas::objectAt(QPoint cell) const {
    const QVector<sdk::MapObject> objects = model_->objects();
    // Topmost first: objects are drawn in order, so search from the end.
    for (int i = objects.size() - 1; i >= 0; --i) {
        if (objects.at(i).cells.contains(cell)) {
            return objects.at(i).id;
        }
    }
    return {};
}

void MapCanvas::paintLine(QPoint from, QPoint to, int value) {
    // Bresenham, so fast drags leave no gaps.
    int x0 = from.x();
    int y0 = from.y();
    const int x1 = to.x();
    const int y1 = to.y();
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        const QPoint cell(x0, y0);
        if (insideMap(cell) && model_->isCellEditable(cell)) {
            model_->paintCell(cell, value);
        } else if (insideMap(cell) && !lockReported_) {
            // A stroke runs into a locked cell: why, once per stroke.
            lockReported_ = true;
            emit actionFailed(model_->lockReason(cell));
        }
        if (x0 == x1 && y0 == y1) {
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

QScrollArea* MapCanvas::scrollArea() const {
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(widget)) return area;
    }
    return nullptr;
}

void MapCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus();
    // The middle button drags the map around, whatever the tool.
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        panFrom_ = event->globalPos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    // A second button during a stroke or a drag would restart it and lose
    // what was painted so far.
    if (painting_ || !dragging_.isEmpty()) {
        return;
    }
    const QPoint cell = cellAt(event->pos());
    if (!insideMap(cell) || model_->isReadOnly()) {
        if (insideMap(cell) && tool_ == QLatin1String("select")) {
            model_->selectObject(objectAt(cell));
        }
        return;
    }
    QString error;
    if (tool_ == QLatin1String("paint")) {
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
            return;
        }
        // Right button paints the first brush, which is "empty" by convention.
        const QVector<sdk::CellBrush> brushes = model_->brushes();
        if (event->button() == Qt::RightButton && model_->rightClickPicks()) {
            const int value = model_->cellValue(cell);
            if (value >= 0) {
                model_->setCurrentBrush(value);
            }
            return;
        }
        lockReported_ = false;
        paintValue_ =
            event->button() == Qt::RightButton && !brushes.isEmpty() ? brushes.first().value : model_->currentBrush();
        painting_ = true;
        paintButton_ = event->button();
        lastPaintCell_ = cell;
        model_->beginStroke();
        paintLine(cell, cell, paintValue_);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (tool_ == QLatin1String("pick")) {
        const int value = model_->cellValue(cell);
        if (value >= 0) {
            model_->setCurrentBrush(value);
        }
        return;
    }
    if (tool_ == QLatin1String("select")) {
        const QString hit = objectAt(cell);
        model_->selectObject(hit);
        if (hit.isEmpty()) {
            return;
        }
        for (const sdk::MapObject& object : model_->objects()) {
            if (object.id == hit && object.movable) {
                dragging_ = hit;
                dragOffset_ = cell - object.cells.topLeft();
                model_->beginDrag(hit);
            }
        }
    } else if (tool_ == QLatin1String("place")) {
        if (!model_->placeObject(model_->currentLibraryItem(), cell, &error)) {
            emit actionFailed(error);
        }
    } else if (tool_ == QLatin1String("remove")) {
        const QString hit = objectAt(cell);
        if (!hit.isEmpty() && !model_->removeObject(hit, &error)) {
            emit actionFailed(error);
        }
    }
}

void MapCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPoint delta = event->globalPos() - panFrom_;
        panFrom_ = event->globalPos();
        if (QScrollArea* area = scrollArea()) {
            area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - delta.x());
            area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - delta.y());
        }
        return;
    }
    const QPoint cell = cellAt(event->pos());
    setHoverCell(cell);
    if (painting_ && insideMap(cell) && cell != lastPaintCell_) {
        paintLine(lastPaintCell_, cell, paintValue_);
        lastPaintCell_ = cell;
    } else if (!dragging_.isEmpty()) {
        model_->dragObject(dragging_, cell - dragOffset_);
    }
}

void MapCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_) {
        if (event->button() == Qt::MiddleButton) {
            panning_ = false;
            updateCursor();
        }
        return;
    }
    if (painting_) {
        // Only the button that began the stroke ends it.
        if (event->button() != paintButton_) {
            return;
        }
        painting_ = false;
        paintButton_ = Qt::NoButton;
        model_->endStroke();
    }
    if (!dragging_.isEmpty()) {
        dragging_.clear();
        model_->endDrag();
    }
}

void MapCanvas::leaveEvent(QEvent*) { setHoverCell(QPoint(-1, -1)); }

void MapCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1, event->position().toPoint());
        event->accept();
        return;
    }
    event->ignore();
}

void MapCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        const QString selected = model_->selectedObject();
        QString error;
        if (!selected.isEmpty() && !model_->isReadOnly() && !model_->removeObject(selected, &error)) {
            emit actionFailed(error);
        }
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        model_->selectObject(QString());
        return;
    }
    QWidget::keyPressEvent(event);
}

void MapCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    // With the select tool a double click opens what the object stands for.
    if (event->button() == Qt::LeftButton && tool_ == QLatin1String("select")) {
        const QString hit = objectAt(cellAt(event->pos()));
        if (!hit.isEmpty()) {
            model_->activateObject(hit);
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MapCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kLibraryItemMime) && !model_->isReadOnly()) {
        event->acceptProposedAction();
    }
}

void MapCanvas::dragMoveEvent(QDragMoveEvent* event) {
    setHoverCell(cellAt(event->pos()));
    if (insideMap(hoverCell_)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MapCanvas::dropEvent(QDropEvent* event) {
    const QPoint cell = cellAt(event->pos());
    const QString item = QString::fromUtf8(event->mimeData()->data(kLibraryItemMime));
    QString error;
    if (insideMap(cell) && !model_->placeObject(item, cell, &error)) {
        emit actionFailed(error);
    }
    event->acceptProposedAction();
}

void MapCanvas::paintEvent(QPaintEvent* event) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    // Everything per pixel or per cell is limited to the area being repainted.
    const QRect dirty = event->rect();
    painter.fillRect(dirty, t.canvas);
    paint::dotGrid(painter, dirty, 16, theme::canvas::kGridDot);

    const QRect map = mapRect();
    const int cw = std::max(1, scaled(model_->cellSize().width()));
    const int ch = std::max(1, scaled(model_->cellSize().height()));
    const QSize grid = model_->gridSize();

    if (!image_.isNull()) {
        painter.drawImage(map, image_);
    }

    // Cell overlay (collision and similar layers).
    if (model_->showsCellOverlay()) {
        const QVector<sdk::CellBrush> brushes = model_->brushes();
        const QRect cells = dirty.intersected(map);
        const int x0 = std::max(0, (cells.left() - map.left()) / cw);
        const int y0 = std::max(0, (cells.top() - map.top()) / ch);
        const int x1 = std::min(grid.width() - 1, (cells.right() - map.left()) / cw);
        const int y1 = std::min(grid.height() - 1, (cells.bottom() - map.top()) / ch);
        for (int y = y0; !cells.isEmpty() && y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int value = model_->cellValue(QPoint(x, y));
                for (const sdk::CellBrush& brush : brushes) {
                    if (brush.value == value && brush.overlay.isValid()) {
                        painter.fillRect(QRect(map.left() + x * cw, map.top() + y * ch, cw, ch), brush.overlay);
                    }
                }
            }
        }
    }

    if (grid_ && zoom_ >= 8) {
        const QColor line(0, 0, 0, 41);
        for (int x = 1; x < grid.width(); ++x) {
            painter.fillRect(QRect(map.left() + x * cw, map.top(), 1, map.height()), line);
        }
        for (int y = 1; y < grid.height(); ++y) {
            painter.fillRect(QRect(map.left(), map.top() + y * ch, map.width(), 1), line);
        }
    }

    // Pages: dashed separators and labels above the map.
    const int pageColumns = model_->pageColumns();
    if (pageColumns > 0 && pageColumns < grid.width()) {
        QPen dashed(QColor(255, 255, 255, 140));
        dashed.setDashPattern({6, 5});
        painter.setFont(theme::font(theme::TextRole::Mono, 10.5));
        for (int page = 0; page * pageColumns < grid.width(); ++page) {
            const int x = map.left() + page * pageColumns * cw;
            if (page > 0) {
                painter.setPen(dashed);
                painter.drawLine(x, map.top(), x, map.bottom());
            }
            painter.fillRect(QRect(x, map.top() - 24, 1, 16), QColor(255, 255, 255, 90));
            painter.setPen(theme::canvas::kPageLabel);
            painter.drawText(QRect(x + 5, map.top() - 24, 80, 16), Qt::AlignVCenter, tr("Page %1").arg(page));
        }
    }

    paint::cornerMarks(painter, map, 13, 7, theme::canvas::kMark);

    // Objects: dashed outlines, the selection filled with a type tag.
    const QString selected = model_->selectedObject();
    QPen outline(theme::canvas::kObjectOutline);
    outline.setDashPattern({3, 3});
    const sdk::MapObject* selectedObject = nullptr;
    const QVector<sdk::MapObject> objects = model_->objects();
    // Footprints may run past the map edge; keep outlines on the map.
    painter.save();
    painter.setClipRect(map.adjusted(0, 0, 1, 1));
    for (const sdk::MapObject& object : objects) {
        const QRect r(map.left() + object.cells.left() * cw, map.top() + object.cells.top() * ch,
                      object.cells.width() * cw, object.cells.height() * ch);
        if (object.id == selected) {
            selectedObject = &object;
            continue;
        }
        if (!object.outlined) {
            continue;
        }
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r.adjusted(0, 0, -1, -1));
    }
    if (selectedObject) {
        const QRect r(map.left() + selectedObject->cells.left() * cw, map.top() + selectedObject->cells.top() * ch,
                      selectedObject->cells.width() * cw, selectedObject->cells.height() * ch);
        painter.fillRect(r, theme::canvas::kSelectionFill);
        paint::hairlineRect(painter, r, theme::canvas::kSelection, 2.0);
        painter.restore();
        const QFont tagFont = theme::font(theme::TextRole::Mono, 10.0);
        painter.setFont(tagFont);
        const int tagWidth = painter.fontMetrics().horizontalAdvance(selectedObject->label) + 10;
        const QRect tag(r.left() - 2, r.top() - 17, tagWidth, 14);
        painter.fillRect(tag, theme::canvas::kTagFill);
        painter.setPen(theme::canvas::kTagText);
        painter.drawText(tag, Qt::AlignCenter, selectedObject->label);
    } else {
        painter.restore();
    }

    // Hover: the cell under a painting or placing tool.
    if (insideMap(hoverCell_) && !model_->isReadOnly() &&
        (tool_ == QLatin1String("paint") || tool_ == QLatin1String("place"))) {
        const QRect r(map.left() + hoverCell_.x() * cw, map.top() + hoverCell_.y() * ch, cw, ch);
        const bool editable = tool_ != QLatin1String("paint") || model_->isCellEditable(hoverCell_);
        if (tool_ == QLatin1String("place") && !previewIcon_.isNull()) {
            // Where the item lands: itself, see-through, over its footprint
            // (or at its own size when the model gives none).
            const QRect footprint(r.topLeft(), previewSize());
            const QRect picture =
                previewCells_.isEmpty()
                    ? QRect(r.topLeft(), QSize(scaled(previewIcon_.width()), scaled(previewIcon_.height())))
                    : footprint;
            painter.save();
            painter.setOpacity(0.55);
            painter.drawImage(picture, previewIcon_);
            painter.restore();
            paint::hairlineRect(painter, footprint, QColor(255, 255, 255, 200));
        } else {
            paint::hairlineRect(painter, r, editable ? QColor(255, 255, 255, 200) : QColor(255, 255, 255, 60));
        }
    }
}

// ---------------------------------------------------------------- editor

TileMapEditor::TileMapEditor(sdk::TileMapModel* model, QWidget* parent)
    : EditorView(model, parent), map_(model), scroll_(new QScrollArea(this)), canvas_(new MapCanvas(model, scroll_)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setWidget(canvas_);
    scroll_->setProperty("surface", QStringLiteral("canvas"));
    // The last pick on this kind of map, before the sheet shows its own.
    pickLayer_ = map_->currentLayer();
    restorePick();
    pickedItem_ = map_->currentLibraryItem();
    connect(map_, &sdk::EditorModel::selectionChanged, this, &TileMapEditor::rememberPick);
    connect(map_, &sdk::EditorModel::toolsChanged, this, &TileMapEditor::rememberPick);

    // The model's pattern sheet, if it has one, beside the map.
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    split->addWidget(scroll_);
    auto* sheet = new SheetPane(model, split);
    split->addWidget(sheet);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    layout->addWidget(split);

    const QVector<sdk::Tool> tools = map_->tools();
    const bool remembered =
        std::any_of(tools.begin(), tools.end(), [](const sdk::Tool& tool) { return tool.id == lastTool(); });
    tool_ = remembered ? lastTool() : tools.isEmpty() ? QStringLiteral("select") : tools.first().id;
    canvas_->setTool(tool_);
    updatePlacePreview();

    // Ctrl+wheel zooms about the mouse: the picture under it stays put.
    connect(canvas_, &MapCanvas::zoomRequested, this, [this](int delta, QPoint anchor) {
        const int next = zoomStep(zoom(), delta);
        zoomKeepingPoint(
            scroll_, canvas_, anchor, [this] { return canvas_->mapRect(); }, [this, next] { canvas_->setZoom(next); });
        emit toolbarChanged();
    });
    connect(canvas_, &MapCanvas::actionFailed, this, &EditorView::actionFailed);
    connect(map_, &sdk::EditorModel::toolsChanged, this, [this] {
        const QVector<sdk::Tool> current = map_->tools();
        const bool known =
            std::any_of(current.begin(), current.end(), [this](const sdk::Tool& t) { return t.id == tool_; });
        const bool remembered =
            std::any_of(current.begin(), current.end(), [](const sdk::Tool& t) { return t.id == lastTool(); });
        if (remembered && tool_ != lastTool()) {
            // The user's tool is back (another layer offers it again).
            tool_ = lastTool();
            canvas_->setTool(tool_);
        } else if (!known) {
            // A fallback, not the user's choice: the remembered tool stays.
            tool_ = current.isEmpty() ? QStringLiteral("select") : current.first().id;
            canvas_->setTool(tool_);
        }
        updatePlacePreview();
        emit toolbarChanged();
    });
}

void TileMapEditor::rememberPick() {
    if (restoring_) return;
    bool otherLayer = false;
    if (map_->currentLayer() != pickLayer_) {
        // Another layer: its own last pick comes back first.
        pickLayer_ = map_->currentLayer();
        restorePick();
        otherLayer = true;
    }
    Pick& pick = lastPicks()[pickKey(map_)];
    if (!map_->brushes().isEmpty()) pick.brush = map_->currentBrush();
    const QString item = map_->currentLibraryItem();
    if (!item.isEmpty()) pick.item = item;
    if (item != pickedItem_) {
        pickedItem_ = item;
        // An object picked in the library (not a brush, not the layer's own
        // default) is something to put down: the Add tool takes it.
        if (!otherLayer && !item.isEmpty() && map_->brushes().isEmpty() && offersTool(QStringLiteral("place")) &&
            tool_ != QLatin1String("place")) {
            chooseTool(QStringLiteral("place"));
            emit toolbarChanged();
        }
    }
    updatePlacePreview();
}

bool TileMapEditor::offersTool(const QString& id) const {
    const QVector<sdk::Tool> tools = map_->tools();
    return std::any_of(tools.begin(), tools.end(), [&id](const sdk::Tool& tool) { return tool.id == id; });
}

bool TileMapEditor::findLibraryItem(const QString& item, QImage* icon, QSize* cells) const {
    for (const sdk::Library& library : {map_->sheet(), map_->inspect().library}) {
        for (const sdk::LibraryItem& entry : library.items) {
            if (entry.id == item) {
                *icon = entry.icon;
                *cells = entry.cells;
                return true;
            }
        }
    }
    return false;
}

void TileMapEditor::updatePlacePreview() {
    QImage icon;
    QSize cells;
    if (tool_ == QLatin1String("place") && findLibraryItem(map_->currentLibraryItem(), &icon, &cells)) {
        canvas_->setPlacePreview(icon, cells);
    } else {
        canvas_->setPlacePreview(QImage(), QSize());
    }
}

bool TileMapEditor::armPlacement(const QString& item) {
    restoring_ = true;  // a pick of its own: no second arming
    map_->setCurrentLibraryItem(item);
    restoring_ = false;
    // The model may have changed layer to offer the item: that layer's old
    // pick must not come back over it.
    pickLayer_ = map_->currentLayer();
    pickedItem_ = map_->currentLibraryItem();
    if (!offersTool(QStringLiteral("place")) || pickedItem_ != item) return false;
    chooseTool(QStringLiteral("place"));
    rememberPick();
    emit toolbarChanged();
    return true;
}

void TileMapEditor::restorePick() {
    const auto found = lastPicks().constFind(pickKey(map_));
    if (found == lastPicks().constEnd()) return;
    const Pick pick = *found;
    restoring_ = true;
    // brushes() often lists only the current brush, so the model's
    // setCurrentBrush decides whether the value fits this map.
    if (pick.brush && !map_->brushes().isEmpty() && map_->currentBrush() != *pick.brush) {
        map_->setCurrentBrush(*pick.brush);
    }
    // An item only where this map's library offers it.
    if (!pick.item.isEmpty() && map_->currentLibraryItem() != pick.item && libraryHas(pick.item)) {
        map_->setCurrentLibraryItem(pick.item);
    }
    restoring_ = false;
}

bool TileMapEditor::libraryHas(const QString& item) const {
    const auto has = [&item](const sdk::Library& library) {
        return std::any_of(library.items.begin(), library.items.end(),
                           [&item](const sdk::LibraryItem& entry) { return entry.id == item; });
    };
    return has(map_->sheet()) || has(map_->inspect().library);
}

int TileMapEditor::zoom() const { return canvas_->zoom(); }

void TileMapEditor::setZoom(int zoom) {
    // About the middle of the view, so what is in sight stays in sight.
    zoomKeepingPoint(
        scroll_, canvas_, viewportCentre(scroll_, canvas_), [this] { return canvas_->mapRect(); },
        [this, zoom] { canvas_->setZoom(zoom); });
}

bool TileMapEditor::gridVisible() const { return canvas_->gridVisible(); }

void TileMapEditor::setGridVisible(bool visible) { canvas_->setGridVisible(visible); }

void TileMapEditor::chooseTool(const QString& id) {
    tool_ = id;
    lastTool() = id;
    canvas_->setTool(id);
    updatePlacePreview();
}

void TileMapEditor::showEvent(QShowEvent* event) {
    EditorView::showEvent(event);
    if (!fitted_) {
        fitted_ = true;
        // After the splitter has laid out the canvas and any sheet beside it.
        QTimer::singleShot(0, this, &TileMapEditor::fitZoom);
    }
}

void TileMapEditor::fitZoom() {
    const QSize grid = map_->gridSize();
    const QSize cell = map_->cellSize();
    const int mapWidth = std::max(1, grid.width() * cell.width());
    const int mapHeight = std::max(1, grid.height() * cell.height());
    const QSize room = scroll_->viewport()->size() - QSize(kPadSide * 2, kPadTop + kPadBottom);
    // The largest step up to 4x that shows the whole map; a scrolling level
    // (pages) always fits its height and scrolls sideways, so short and long
    // levels open at the same zoom, and a tall strip fits its width (up to
    // 2x) and scrolls down.
    int zoom = kMinZoom;
    for (int step : kZoomSteps) {
        const bool fits = map_->pageColumns() > 0 ? mapHeight * step / 4 <= room.height()
                          : map_->scrollsDown()
                              ? mapWidth * step / 4 <= room.width()
                              : mapWidth * step / 4 <= room.width() && mapHeight * step / 4 <= room.height();
        if (step <= (map_->scrollsDown() ? 8 : 16) && fits) zoom = step;
    }
    setZoom(zoom);
    emit toolbarChanged();
}

QWidget* TileMapEditor::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // The layers come first: they stay put while the tools after them
    // change with the layer.
    const QVector<sdk::MapLayer> layers = map_->layers();
    if (layers.size() > 1) {
        auto* segment = new ui::SegmentedControl(bar);
        for (const sdk::MapLayer& layer : layers) {
            segment->addSegment(layer.label, layer.id);
        }
        segment->setCurrentId(map_->currentLayer());
        connect(segment, &ui::SegmentedControl::currentChanged, this,
                [this](int, const QString& id) { map_->setCurrentLayer(id); });
        layout->addWidget(segment);
        layout->addWidget(toolbarSeparator(bar), 0, Qt::AlignVCenter);
    }

    const QVector<sdk::Tool> tools = map_->tools();
    if (!tools.isEmpty()) {
        auto* segment = new ui::SegmentedControl(bar);
        for (const sdk::Tool& tool : tools) {
            segment->addIconSegment(tool.icon, tool.label, tool.id);
        }
        segment->setCurrentId(tool_);
        connect(segment, &ui::SegmentedControl::currentChanged, this,
                [this](int, const QString& id) { chooseTool(id); });
        layout->addWidget(segment);
        layout->addWidget(toolbarSeparator(bar), 0, Qt::AlignVCenter);
    }

    const QVector<sdk::CellBrush> brushes = map_->brushes();
    if (brushes.size() > 1) {
        auto* segment = new ui::SegmentedControl(bar);
        for (const sdk::CellBrush& brush : brushes) {
            segment->addSegment(brush.label, QString::number(brush.value));
        }
        segment->setCurrentId(QString::number(map_->currentBrush()));
        connect(segment, &ui::SegmentedControl::currentChanged, this,
                [this](int, const QString& id) { map_->setCurrentBrush(id.toInt()); });
        layout->addWidget(segment);
        layout->addWidget(toolbarSeparator(bar), 0, Qt::AlignVCenter);
    }

    layout->addWidget(new ui::Label(tr("View"), ui::Label::Role::Caps, bar));
    for (const sdk::ViewOption& option : map_->viewOptions()) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        if (option.toggle) {
            chip->setCheckable(true);
            chip->setChecked(option.value == QLatin1String("On"));
            connect(chip, &QAbstractButton::toggled, this, [this, chip, key](bool on) {
                chip->setValue(on ? QStringLiteral("On") : QStringLiteral("Off"));
                map_->setViewOption(key, chip->value());
            });
        } else if (!option.choices.isEmpty()) {
            chip->setChoices(option.choices);
            connect(chip, &ui::ViewChip::valueChosen, this,
                    [this, key](const QString& value) { map_->setViewOption(key, value); });
        }
        layout->addWidget(chip);
    }

    auto* gridChip = new ui::ViewChip(tr("Grid"), gridVisible() ? QStringLiteral("On") : QStringLiteral("Off"), bar);
    gridChip->setCheckable(true);
    gridChip->setChecked(gridVisible());
    connect(gridChip, &QAbstractButton::toggled, this, [this, gridChip](bool on) {
        gridChip->setValue(on ? QStringLiteral("On") : QStringLiteral("Off"));
        setGridVisible(on);
    });
    layout->addWidget(gridChip);

    QStringList zooms;
    for (int step : kZoomSteps) {
        zooms << zoomLabel(step);
    }
    auto* zoomChip = new ui::ViewChip(tr("Zoom"), zoomLabel(zoom()), bar);
    zoomChip->setChoices(zooms);
    connect(zoomChip, &ui::ViewChip::valueChosen, this, [this](const QString& value) {
        for (int step : kZoomSteps) {
            if (zoomLabel(step) == value) setZoom(step);
        }
    });
    layout->addWidget(zoomChip);
    layout->addStretch(1);
    return bar;
}

}  // namespace rl::editors
