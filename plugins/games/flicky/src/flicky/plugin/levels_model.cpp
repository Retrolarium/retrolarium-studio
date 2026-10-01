#include "flicky/plugin/levels_model.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "flicky/plugin/objects.hpp"

#include <QPainter>

#include <algorithm>
#include <limits>

namespace rl::flicky {

namespace {

const QString kObjects = QStringLiteral("objects");
const QString kLayout = QStringLiteral("layout");
const QString kBackground = QStringLiteral("background");
constexpr int kBonusPhase = 60;  // frames after ready: the settled bonus scene

QString two(int value) {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
}

QImage errorImage(const QString& message) {
    QImage image(objects::kMapWidth * 8, objects::kMapHeight * 8, QImage::Format_RGB32);
    image.fill(QColor(0x20, 0x16, 0x18));
    QPainter painter(&image);
    painter.setPen(QColor(0xff, 0xb4, 0xa8));
    painter.drawText(image.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                     message);
    return image;
}

}  // namespace

LevelsModel::LevelsModel(FlickySession* session, int round)
    : TileMapModel(session), session_(session), round_(round) {
    connect(session_, &FlickySession::levelsChanged, this, [this] {
        // An undo can remove the selected placement.
        const Ref ref = parseId(selected_);
        if (ref.isValid()) {
            const auto positions =
                session_->levels().objectPositions(layoutFor(ref.field), ref.field);
            if (ref.index >= positions.size()) {
                selected_.clear();
            }
        }
        emit contentChanged();
    });
    // The scene draws with the bank art and the round palettes.
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
    const QStringList fields = layerFields();
    libraryItem_ = fields.isEmpty() ? QString() : fields.first();
}

LevelsModel::Ref LevelsModel::parseId(const QString& id) {
    const int colon = id.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0) {
        return {};
    }
    bool ok = false;
    const int index = id.mid(colon + 1).toInt(&ok);
    return ok ? Ref{id.left(colon), index} : Ref{};
}

QString LevelsModel::makeId(const QString& field, int index) {
    return QStringLiteral("%1:%2").arg(field).arg(index);
}

QString LevelsModel::title() const {
    return QStringLiteral("Round ") + two(round_ + 1);
}

bool LevelsModel::isReadOnly() const {
    return objects::isBonusRound(round_);
}

QString LevelsModel::readOnlyReason() const {
    return isReadOnly()
               ? QStringLiteral("Bonus rounds are built from the bonus tables, not from a layout. "
                                "Editing seesaw, tiger and chick placements is not ported yet.")
               : QString();
}

QStringList LevelsModel::layerFields() const {
    const QString layer = session_->levelLayer();
    if (layer == kBackground) {
        return objects::backgroundLayerFields();
    }
    if (layer == kObjects) {
        return objects::objectLayerFields();
    }
    return {};
}

int LevelsModel::layoutFor(const QString& field) const {
    return session_->levels().objectLayoutForRound(round_, field);
}

QString LevelsModel::roundList(const QVector<int>& rounds) const {
    QStringList names;
    for (int round : rounds) {
        names << QString::number(round + 1);
    }
    return names.isEmpty() ? QStringLiteral("—") : names.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------- geometry

QSize LevelsModel::gridSize() const {
    return {objects::kMapWidth, objects::kMapHeight};
}

QSize LevelsModel::cellSize() const {
    return {8, 8};
}

QVector<sdk::MapLayer> LevelsModel::layers() const {
    if (isReadOnly()) {
        return {};
    }
    return {{kObjects, QStringLiteral("Objects")},
            {kLayout, QStringLiteral("Layout")},
            {kBackground, QStringLiteral("Background")}};
}

QString LevelsModel::currentLayer() const {
    return session_->levelLayer();
}

void LevelsModel::setCurrentLayer(const QString& id) {
    if (id == session_->levelLayer()) {
        return;
    }
    session_->setLevelLayer(id);
    selected_.clear();
    const QStringList fields = layerFields();
    libraryItem_ = fields.isEmpty() ? QString() : fields.first();
    emit toolsChanged();
    emit contentChanged();
}

QVector<sdk::Tool> LevelsModel::tools() const {
    if (isReadOnly()) {
        return {{QStringLiteral("select"), QStringLiteral("Select"), QStringLiteral("select")}};
    }
    if (session_->levelLayer() == kLayout) {
        return {{QStringLiteral("paint"), QStringLiteral("Paint collision (right button erases)"),
                 QStringLiteral("pencil")}};
    }
    return {{QStringLiteral("select"), QStringLiteral("Select and move"), QStringLiteral("select")},
            {QStringLiteral("place"), QStringLiteral("Place the library object"), QStringLiteral("place")},
            {QStringLiteral("remove"), QStringLiteral("Remove objects"), QStringLiteral("erase")}};
}

QImage LevelsModel::render() const {
    try {
        if (isReadOnly()) {
            return session_->renderer().renderBonusPhase(session_->levels(), round_, kBonusPhase);
        }
        const QString layer = session_->levelLayer();
        const auto mode = layer == kLayout       ? FlickyLevelSceneRenderer::Layer::Layout
                          : layer == kBackground ? FlickyLevelSceneRenderer::Layer::Background
                                                 : FlickyLevelSceneRenderer::Layer::Objects;
        return session_->renderer().renderLayer(session_->levels(), round_, mode);
    } catch (const std::exception& e) {
        return errorImage(QString::fromUtf8(e.what()));
    }
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> LevelsModel::brushes() const {
    if (isReadOnly() || session_->levelLayer() != kLayout) {
        return {};
    }
    // Solid cells are mostly platforms, already orange in the scene; tinting
    // the empty ones too keeps the two apart.
    return {{0, QStringLiteral("Empty"), QColor(45, 130, 205, 96)},
            {1, QStringLiteral("Solid"), QColor(250, 150, 30, 175)}};
}

void LevelsModel::setCurrentBrush(int value) {
    brush_ = value == 0 ? 0 : 1;
}

bool LevelsModel::showsCellOverlay() const {
    return !isReadOnly() && (session_->levelLayer() == kLayout || session_->collisionOverlay());
}

int LevelsModel::cellValue(QPoint cell) const {
    return session_->levels().cell(session_->levels().layoutForRound(round_), cell.x(), cell.y());
}

bool LevelsModel::isCellEditable(QPoint cell) const {
    return !isReadOnly() && session_->levelLayer() == kLayout && cell.y() >= objects::kLockedRows &&
           cell.x() >= 0 && cell.x() < objects::kMapWidth && cell.y() < objects::kMapHeight;
}

void LevelsModel::beginStroke() {
    before_ = session_->levels();
    changed_ = false;
}

bool LevelsModel::paintCell(QPoint cell, int value) {
    if (!before_ || !isCellEditable(cell) || cellValue(cell) == value) {
        return false;
    }
    FlickyLevelDocument next = session_->levels();
    try {
        next.setCell(next.layoutForRound(round_), cell.x(), cell.y(), value);
    } catch (const std::exception&) {
        return false;
    }
    changed_ = true;
    session_->setLevelsLive(next);
    return true;
}

void LevelsModel::endStroke() {
    if (before_ && changed_) {
        session_->commitLevels(*before_, session_->levels(),
                               QStringLiteral("Paint collision in layout %1")
                                   .arg(two(session_->levels().layoutForRound(round_))));
    }
    before_.reset();
    changed_ = false;
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> LevelsModel::objects() const {
    QVector<sdk::MapObject> result;
    if (isReadOnly()) {
        return result;
    }
    const FlickyLevelDocument& levels = session_->levels();
    for (const QString& field : layerFields()) {
        const QVector<QPoint> positions = levels.objectPositions(layoutFor(field), field);
        const QSize size = objects::footprint(field);
        for (int i = 0; i < positions.size(); ++i) {
            const QPoint p = positions.at(i);
            if (p.y() >= objects::kMapHeight) {
                continue;  // stored but off the visible map
            }
            sdk::MapObject object;
            object.id = makeId(field, i);
            object.label = positions.size() > 1 || objects::isVariable(field)
                               ? QStringLiteral("%1 #%2").arg(objects::title(field)).arg(i + 1)
                               : objects::title(field);
            object.cells = QRect(p, size);
            result.append(object);
        }
    }
    return result;
}

void LevelsModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

void LevelsModel::beginDrag(const QString& id) {
    before_ = session_->levels();
    changed_ = false;
    selectObject(id);
}

bool LevelsModel::dragObject(const QString& id, QPoint topLeftCell) {
    const Ref ref = parseId(id);
    if (!before_ || !ref.isValid() || isReadOnly()) {
        return false;
    }
    const QPoint target(std::clamp(topLeftCell.x(), 0, objects::kMapWidth - 1),
                        std::clamp(topLeftCell.y(), 0, objects::kMapHeight - 1));
    const int layout = layoutFor(ref.field);
    const QVector<QPoint> positions = session_->levels().objectPositions(layout, ref.field);
    if (ref.index >= positions.size() || positions.at(ref.index) == target) {
        return false;
    }
    FlickyLevelDocument next = session_->levels();
    try {
        next.setObjectPosition(layout, ref.field, ref.index, target);
    } catch (const std::exception&) {
        return false;
    }
    changed_ = true;
    session_->setLevelsLive(next);
    return true;
}

void LevelsModel::endDrag() {
    if (before_ && changed_) {
        const Ref ref = parseId(selected_);
        session_->commitLevels(*before_, session_->levels(),
                               QStringLiteral("Move %1").arg(objects::title(ref.field)));
    }
    before_.reset();
    changed_ = false;
}

bool LevelsModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const QString field = libraryItem;
    if (isReadOnly() || !layerFields().contains(field)) {
        if (error) {
            *error = QStringLiteral("Pick an object from the library first.");
        }
        return false;
    }
    const int layout = layoutFor(field);
    const QVector<QPoint> positions = session_->levels().objectPositions(layout, field);
    int selectIndex = -1;
    QString text;
    std::function<void(FlickyLevelDocument&)> edit;
    if (objects::isVariable(field)) {
        selectIndex = positions.size();
        text = QStringLiteral("Add %1").arg(objects::title(field));
        edit = [layout, field, cell](FlickyLevelDocument& levels) {
            levels.addObjectPosition(layout, field, cell);
        };
    } else {
        // Fixed placements cannot be added; the nearest one moves instead.
        int nearest = 0;
        int best = std::numeric_limits<int>::max();
        for (int i = 0; i < positions.size(); ++i) {
            const int distance = (positions.at(i) - cell).manhattanLength();
            if (distance < best) {
                best = distance;
                nearest = i;
            }
        }
        if (positions.isEmpty()) {
            if (error) {
                *error = QStringLiteral("%1 has no placement in this layout.").arg(objects::title(field));
            }
            return false;
        }
        selectIndex = nearest;
        text = QStringLiteral("Move %1").arg(objects::title(field));
        edit = [layout, field, nearest, cell](FlickyLevelDocument& levels) {
            levels.setObjectPosition(layout, field, nearest, cell);
        };
    }
    if (!session_->editLevels(text, edit, error)) {
        return false;
    }
    selectObject(makeId(field, selectIndex));
    return true;
}

bool LevelsModel::removeObject(const QString& id, QString* error) {
    const Ref ref = parseId(id);
    if (!ref.isValid() || isReadOnly()) {
        return false;
    }
    if (!objects::isVariable(ref.field)) {
        if (error) {
            *error = QStringLiteral("%1 is a fixed placement: move it instead of removing it.")
                         .arg(objects::title(ref.field));
        }
        return false;
    }
    const int layout = layoutFor(ref.field);
    const QString field = ref.field;
    const int index = ref.index;
    if (!session_->editLevels(QStringLiteral("Remove %1").arg(objects::title(field)),
                              [layout, field, index](FlickyLevelDocument& levels) {
                                  levels.removeObjectPosition(layout, field, index);
                              },
                              error)) {
        return false;
    }
    selectObject(QString());
    return true;
}

void LevelsModel::setCurrentLibraryItem(const QString& id) {
    if (id != libraryItem_) {
        libraryItem_ = id;
        emit selectionChanged();
    }
}

// ---------------------------------------------------------------- inspector

QVector<sdk::ViewOption> LevelsModel::viewOptions() const {
    if (isReadOnly() || session_->levelLayer() == kLayout) {
        return {};
    }
    sdk::ViewOption collision;
    collision.key = QStringLiteral("collision");
    collision.label = QStringLiteral("Collision");
    collision.value = session_->collisionOverlay() ? QStringLiteral("On") : QStringLiteral("Off");
    collision.toggle = true;
    return {collision};
}

void LevelsModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("collision")) {
        session_->setCollisionOverlay(value == QLatin1String("On"));
        emit contentChanged();
    }
}

sdk::Budget LevelsModel::budget() const {
    sdk::Budget budget;
    try {
        const QPair<int, int> used = session_->levels().capacityUsage();
        const QPair<int, int> limit = session_->levels().capacityLimits();
        budget.used = used.first;
        budget.capacity = limit.first;
        budget.text = QStringLiteral("level data %1 / %2 B").arg(used.first).arg(limit.first);
    } catch (const std::exception& e) {
        budget.text = QString::fromUtf8(e.what());
    }
    return budget;
}

sdk::Library LevelsModel::library() const {
    sdk::Library library;
    if (isReadOnly() || session_->levelLayer() == kLayout) {
        return library;
    }
    library.hint = QStringLiteral("drag onto the map");
    for (const QString& field : layerFields()) {
        sdk::LibraryItem item;
        item.id = field;
        item.label = objects::title(field);
        item.icon = session_->placementPreview(field, round_);
        item.hint = objects::isVariable(field)
                        ? QStringLiteral("%1: drop to add one").arg(item.label)
                        : QStringLiteral("%1: drop to move the existing one").arg(item.label);
        library.items.append(item);
    }
    return library;
}

sdk::Inspection LevelsModel::inspect() const {
    if (isReadOnly() || session_->levelLayer() != kLayout) {
        const Ref ref = parseId(selected_);
        if (!isReadOnly() && ref.isValid()) {
            return inspectObject(ref);
        }
        return inspectRound();
    }
    return inspectLayout();
}

sdk::Inspection LevelsModel::inspectRound() const {
    const FlickyLevelDocument& levels = session_->levels();
    sdk::Inspection inspection;
    inspection.kicker = isReadOnly() ? QStringLiteral("Bonus round") : QStringLiteral("Round");
    inspection.title = title();
    inspection.engineTag = QStringLiteral("flicky.levels.v1");
    if (!isReadOnly()) {
        const int layout = levels.layoutForRound(round_);
        sdk::Field layoutField;
        layoutField.key = QStringLiteral("layout");
        layoutField.label = QStringLiteral("Layout");
        layoutField.value = two(layout);
        layoutField.monospace = true;
        sdk::Field shared;
        shared.key = QStringLiteral("shared");
        shared.label = QStringLiteral("Used by");
        shared.value = QStringLiteral("rounds ") + roundList(levels.roundsForLayout(layout));
        inspection.fields = {layoutField, shared};
        inspection.note = QStringLiteral("Pick an object on the map, or drag one from the library. "
                                         "Layouts are shared: edits show in every round listed above.");
        inspection.library = library();
        inspection.engine = {{QStringLiteral("Record"), QStringLiteral("Level_Data%1").arg(layout)},
                             {QStringLiteral("Collision"),
                              QStringLiteral("%1 bytes RLE").arg(levels.encodedCollision(layout).size())},
                             {QStringLiteral("Level bytes"),
                              QString::number(levels.encodedLevel(layout).size())},
                             {QStringLiteral("Special bytes"),
                              QString::number(levels.encodedSpecial(layout).size())}};
    }
    return inspection;
}

sdk::Inspection LevelsModel::inspectLayout() const {
    const FlickyLevelDocument& levels = session_->levels();
    const int layout = levels.layoutForRound(round_);
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Collision layout");
    inspection.title = QStringLiteral("Layout ") + two(layout);
    sdk::Field brush;
    brush.key = QStringLiteral("brush");
    brush.label = QStringLiteral("Brush");
    brush.type = sdk::Field::Type::Choice;
    brush.choices = QStringList{QStringLiteral("Solid"), QStringLiteral("Empty")};
    brush.value = brush_ == 1 ? QStringLiteral("Solid") : QStringLiteral("Empty");
    sdk::Field shared;
    shared.key = QStringLiteral("shared");
    shared.label = QStringLiteral("Used by");
    shared.value = QStringLiteral("rounds ") + roundList(levels.roundsForLayout(layout));
    int solid = 0;
    for (std::uint8_t value : levels.grid(layout)) {
        solid += value != 0;
    }
    sdk::Field cells;
    cells.key = QStringLiteral("cells");
    cells.label = QStringLiteral("Solid cells");
    cells.value = QString::number(solid);
    cells.monospace = true;
    inspection.fields = {brush, shared, cells};
    inspection.note = QStringLiteral("Paint with the left button, erase with the right. The top two "
                                     "rows are generated by the game and cannot be painted.");
    inspection.engineTag = QStringLiteral("flicky.levels.v1");
    const auto stream = levels.encodedCollision(layout);
    inspection.engine = {{QStringLiteral("Record"), QStringLiteral("Level_Data%1").arg(layout)},
                         {QStringLiteral("Encoding"), QStringLiteral("RLE from cell $40")},
                         {QStringLiteral("Stream"), QStringLiteral("%1 bytes").arg(stream.size())},
                         {QStringLiteral("Changed"),
                          levels.grid(layout) == levels.baselineGrid(layout) ? QStringLiteral("no")
                                                                             : QStringLiteral("yes")}};
    return inspection;
}

sdk::Inspection LevelsModel::inspectObject(const Ref& ref) const {
    const FlickyLevelDocument& levels = session_->levels();
    const int layout = layoutFor(ref.field);
    const QVector<QPoint> positions = levels.objectPositions(layout, ref.field);
    const QPoint position = ref.index < positions.size() ? positions.at(ref.index) : QPoint();
    sdk::Inspection inspection;
    inspection.kicker = QStringLiteral("Round object");
    inspection.title = objects::isVariable(ref.field) || positions.size() > 1
                           ? QStringLiteral("%1 #%2").arg(objects::title(ref.field)).arg(ref.index + 1)
                           : objects::title(ref.field);

    sdk::Field type;
    type.key = QStringLiteral("type");
    type.label = QStringLiteral("Type");
    const QStringList candidates = objects::typeCandidates(ref.field);
    if (candidates.size() > 1) {
        type.type = sdk::Field::Type::Choice;
        for (const QString& candidate : candidates) {
            type.choices << objects::title(candidate);
        }
    }
    type.value = objects::title(ref.field);

    sdk::Field place;
    place.key = QStringLiteral("position");
    place.label = QStringLiteral("Position");
    place.type = sdk::Field::Type::Text;
    place.value = QStringLiteral("%1, %2").arg(position.x()).arg(position.y());
    place.hint = QStringLiteral("cell");

    const QSize footprint = objects::footprint(ref.field);
    sdk::Field size;
    size.key = QStringLiteral("size");
    size.label = QStringLiteral("Size");
    size.value = QStringLiteral("%1 × %2").arg(footprint.width()).arg(footprint.height());
    size.hint = QStringLiteral("visible");

    sdk::Field shared;
    shared.key = QStringLiteral("shared");
    shared.label = QStringLiteral("Used by");
    shared.value = QStringLiteral("rounds ") + roundList(levels.roundsForObjectLayout(layout, ref.field));

    inspection.fields = {type, place, size, shared};
    inspection.note = objects::isVariable(ref.field)
                          ? QStringLiteral("Drag on the map to move. Delete removes it.")
                          : QStringLiteral("Drag on the map to move. Every layout has exactly this one.");
    inspection.library = library();
    inspection.engineTag = QStringLiteral("flicky.levels.v1");
    inspection.engine = {
        {QStringLiteral("Record"), QStringLiteral("Level_Data%1").arg(layout)},
        {QStringLiteral("Field"), ref.field},
        {QStringLiteral("Index"), QStringLiteral("%1 of %2").arg(ref.index + 1).arg(positions.size())},
        {QStringLiteral("Encoding"), objects::encoding(ref.field)},
        {QStringLiteral("Table"), FlickyLevelDocument::isSpecialObjectField(ref.field)
                                      ? QStringLiteral("Level_SpecialTilePointers")
                                      : QStringLiteral("Level_DataPointers")},
    };
    return inspection;
}

bool LevelsModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("brush")) {
        setCurrentBrush(value.toString() == QLatin1String("Solid") ? 1 : 0);
        emit toolsChanged();
        return true;
    }
    const Ref ref = parseId(selected_);
    if (!ref.isValid() || isReadOnly()) {
        return TileMapModel::setField(key, value, error);
    }
    const int layout = layoutFor(ref.field);
    if (key == QLatin1String("position")) {
        const QStringList parts = value.toString().split(QLatin1Char(','));
        bool okX = false;
        bool okY = false;
        const int x = parts.value(0).trimmed().toInt(&okX);
        const int y = parts.value(1).trimmed().toInt(&okY);
        if (parts.size() != 2 || !okX || !okY || x < 0 || x >= objects::kMapWidth || y < 0 ||
            y >= objects::kMapHeight) {
            if (error) {
                *error = QStringLiteral("Position is \"x, y\" with x 0–31 and y 0–27.");
            }
            return false;
        }
        const QString field = ref.field;
        const int index = ref.index;
        return session_->editLevels(QStringLiteral("Move %1").arg(objects::title(field)),
                                    [layout, field, index, x, y](FlickyLevelDocument& levels) {
                                        levels.setObjectPosition(layout, field, index, QPoint(x, y));
                                    },
                                    error);
    }
    if (key == QLatin1String("type")) {
        QString target;
        for (const QString& candidate : objects::typeCandidates(ref.field)) {
            if (objects::title(candidate) == value.toString()) {
                target = candidate;
            }
        }
        if (target.isEmpty() || target == ref.field) {
            return target == ref.field;
        }
        const QString field = ref.field;
        const int index = ref.index;
        const int targetLayout = layoutFor(target);
        const QVector<QPoint> positions = session_->levels().objectPositions(layout, field);
        if (index >= positions.size()) {
            return false;
        }
        const QPoint position = positions.at(index);
        const int newIndex = session_->levels().objectPositions(targetLayout, target).size();
        if (!session_->editLevels(
                QStringLiteral("Change %1 to %2").arg(objects::title(field), objects::title(target)),
                [=](FlickyLevelDocument& levels) {
                    levels.removeObjectPosition(layout, field, index);
                    levels.addObjectPosition(targetLayout, target, position);
                },
                error)) {
            return false;
        }
        if (!layerFields().contains(target)) {
            selectObject(QString());
        } else {
            selectObject(makeId(target, newIndex));
        }
        return true;
    }
    return TileMapModel::setField(key, value, error);
}

QString LevelsModel::startPoint() const {
    return QStringLiteral("Round ") + two(round_ + 1);
}

QString LevelsModel::statusText() const {
    if (isReadOnly()) {
        return QStringLiteral("%1 · bonus round (read only)").arg(title());
    }
    const Ref ref = parseId(selected_);
    if (ref.isValid()) {
        const auto positions = session_->levels().objectPositions(layoutFor(ref.field), ref.field);
        if (ref.index < positions.size()) {
            const QPoint p = positions.at(ref.index);
            return QStringLiteral("%1 at %2, %3 · layout %4")
                .arg(objects::title(ref.field))
                .arg(p.x())
                .arg(p.y())
                .arg(two(layoutFor(ref.field)));
        }
    }
    const int layout = session_->levels().layoutForRound(round_);
    const int shared = session_->levels().roundsForLayout(layout).size();
    return QStringLiteral("%1 · layout %2 · shared by %3 round%4")
        .arg(title(), two(layout))
        .arg(shared)
        .arg(shared == 1 ? QString() : QStringLiteral("s"));
}

}  // namespace rl::flicky
