#include "editors/tilemap_editor.hpp"

#include "editors/library_sheet.hpp"
#include "editors/mime.hpp"
#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/segmented_control.hpp"
#include "widgets/view_chip.hpp"

#include <QDragEnterEvent>
#include <QHBoxLayout>
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
    refresh();
}

void MapCanvas::refresh() {
    image_ = model_->render();
    setMinimumSize(sizeHint());
    update();
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

void MapCanvas::updateCursor() {
    setCursor(tool_ == QLatin1String("select") ? Qt::ArrowCursor : Qt::CrossCursor);
}

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

void MapCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus();
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
        if (!model_->isCellEditable(cell)) {
            emit actionFailed(model_->lockReason(cell));
        }
        paintValue_ = event->button() == Qt::RightButton && !brushes.isEmpty()
                          ? brushes.first().value
                          : model_->currentBrush();
        painting_ = true;
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
    const QPoint cell = cellAt(event->pos());
    if (cell != hoverCell_) {
        hoverCell_ = cell;
        update();
    }
    if (painting_ && insideMap(cell) && cell != lastPaintCell_) {
        paintLine(lastPaintCell_, cell, paintValue_);
        lastPaintCell_ = cell;
    } else if (!dragging_.isEmpty()) {
        model_->dragObject(dragging_, cell - dragOffset_);
    }
}

void MapCanvas::mouseReleaseEvent(QMouseEvent*) {
    if (painting_) {
        painting_ = false;
        model_->endStroke();
    }
    if (!dragging_.isEmpty()) {
        dragging_.clear();
        model_->endDrag();
    }
}

void MapCanvas::leaveEvent(QEvent*) {
    hoverCell_ = QPoint(-1, -1);
    update();
}

void MapCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(event->angleDelta().y() > 0 ? 1 : -1);
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

void MapCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(kLibraryItemMime) && !model_->isReadOnly()) {
        event->acceptProposedAction();
    }
}

void MapCanvas::dragMoveEvent(QDragMoveEvent* event) {
    hoverCell_ = cellAt(event->pos());
    update();
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

void MapCanvas::paintEvent(QPaintEvent*) {
    const theme::Tokens& t = theme::current();
    QPainter painter(this);
    painter.fillRect(rect(), t.canvas);
    paint::dotGrid(painter, rect(), 16, theme::canvas::kGridDot);

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
        for (int y = 0; y < grid.height(); ++y) {
            for (int x = 0; x < grid.width(); ++x) {
                const int value = model_->cellValue(QPoint(x, y));
                for (const sdk::CellBrush& brush : brushes) {
                    if (brush.value == value && brush.overlay.isValid()) {
                        painter.fillRect(QRect(map.left() + x * cw, map.top() + y * ch, cw, ch),
                                         brush.overlay);
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
            painter.drawText(QRect(x + 5, map.top() - 24, 80, 16), Qt::AlignVCenter,
                             QStringLiteral("Page %1").arg(page));
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
        const QRect r(map.left() + selectedObject->cells.left() * cw,
                      map.top() + selectedObject->cells.top() * ch,
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
        paint::hairlineRect(painter, r, editable ? QColor(255, 255, 255, 200) : QColor(255, 255, 255, 60));
    }
}

// ---------------------------------------------------------------- editor

TileMapEditor::TileMapEditor(sdk::TileMapModel* model, QWidget* parent)
    : EditorView(model, parent),
      map_(model),
      scroll_(new QScrollArea(this)),
      canvas_(new MapCanvas(model, scroll_)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setWidget(canvas_);
    scroll_->setProperty("surface", QStringLiteral("canvas"));
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
    tool_ = tools.isEmpty() ? QStringLiteral("select") : tools.first().id;
    canvas_->setTool(tool_);

    connect(canvas_, &MapCanvas::zoomRequested, this, [this](int delta) {
        setZoom(zoomStep(zoom(), delta));
        emit toolbarChanged();
    });
    connect(canvas_, &MapCanvas::actionFailed, this, &EditorView::actionFailed);
    connect(map_, &sdk::EditorModel::toolsChanged, this, [this] {
        const QVector<sdk::Tool> current = map_->tools();
        const bool known = std::any_of(current.begin(), current.end(),
                                       [this](const sdk::Tool& t) { return t.id == tool_; });
        if (!known) {
            chooseTool(current.isEmpty() ? QStringLiteral("select") : current.first().id);
        }
        emit toolbarChanged();
    });
}

int TileMapEditor::zoom() const {
    return canvas_->zoom();
}

void TileMapEditor::setZoom(int zoom) {
    canvas_->setZoom(zoom);
}

bool TileMapEditor::gridVisible() const {
    return canvas_->gridVisible();
}

void TileMapEditor::setGridVisible(bool visible) {
    canvas_->setGridVisible(visible);
}

void TileMapEditor::chooseTool(const QString& id) {
    tool_ = id;
    canvas_->setTool(id);
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
                          : map_->scrollsDown() ? mapWidth * step / 4 <= room.width()
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

    layout->addWidget(new ui::Label(QStringLiteral("View"), ui::Label::Role::Caps, bar));
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

    auto* gridChip = new ui::ViewChip(QStringLiteral("Grid"),
                                      gridVisible() ? QStringLiteral("On") : QStringLiteral("Off"), bar);
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
    auto* zoomChip = new ui::ViewChip(QStringLiteral("Zoom"), zoomLabel(zoom()), bar);
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
