#include "doraemon/plugin/block_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace {

const char* const kQuarters[4] = {"top left", "top right", "bottom left", "bottom right"};

// The number in a label made from `pattern` (a translated "BG %1"),
// matched whole: a translation may place the number anywhere. -1 for none.
int labelNumber(const QString& label, const QString& pattern, int count) {
    for (int n = 0; n < count; ++n) {
        if (label == pattern.arg(n)) return n;
    }
    return -1;
}

}  // namespace

DoraemonBlockModel::DoraemonBlockModel(DoraemonSession* session, Kind kind, World world)
    : TileMapModel(session), session_(session), kind_(kind), world_(kind == Kind::Metatile ? World::Two : world) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString DoraemonBlockModel::title() const {
    switch (kind_) {
    case Kind::Small: return tr("%1 small blocks").arg(worldLabel(world_));
    case Kind::Big: return tr("%1 big blocks").arg(worldLabel(world_));
    default: return tr("World 2 metatiles");
    }
}

QString DoraemonBlockModel::resourceId() const {
    return kind_ == Kind::Metatile ? ids::kWorld2Metatiles : hierarchyId(world_);
}

QString DoraemonBlockModel::listKey() const {
    switch (kind_) {
    case Kind::Small: return QStringLiteral("small_blocks");
    case Kind::Big: return QStringLiteral("big_blocks");
    default: return QStringLiteral("tiles");
    }
}

int DoraemonBlockModel::count() const {
    return session_->resource(resourceId()).value(listKey()).toArray().size();
}

QSize DoraemonBlockModel::gridSize() const {
    return {kPerRow * 2, std::max(1, (count() + kPerRow - 1) / kPerRow) * 2};
}

QSize DoraemonBlockModel::cellSize() const {
    // A quarter of a big block is a 16x16 small block.
    return kind_ == Kind::Big ? QSize(16, 16) : QSize(8, 8);
}

std::array<int, 2> DoraemonBlockModel::blockAt(QPoint cell) const {
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kPerRow * 2) return {-1, -1};
    const int block = (cell.y() / 2) * kPerRow + cell.x() / 2;
    if (block >= count()) return {-1, -1};
    return {block, (cell.y() % 2) * 2 + cell.x() % 2};
}

std::array<int, 4> DoraemonBlockModel::quarters(int block) const {
    const auto found = stroke_.constFind(block);
    if (found != stroke_.constEnd()) return *found;
    const QJsonArray q = session_->resource(resourceId()).value(listKey()).toArray().at(block).toArray();
    return {q.at(0).toInt(), q.at(1).toInt(), q.at(2).toInt(), q.at(3).toInt()};
}

QImage DoraemonBlockModel::pieceImage(int piece) const {
    if (kind_ == Kind::Big) return session_->painter().smallBlock(world_, piece, preset_);
    // A pattern in the selected block's row of the preset, or row 0.
    int row = 0;
    if (selected_ >= 0) {
        const QJsonObject r = session_->resource(resourceId());
        row = kind_ == Kind::Metatile
                  ? r.value(QStringLiteral("palettes")).toArray().at(selected_).toInt()
                  : r.value(QStringLiteral("attributes")).toArray().at(selected_).toObject().value(QStringLiteral("palette")).toInt();
    }
    return session_->painter().tile(world_, piece, session_->painter().colours(world_, preset_, row & 3));
}

QImage DoraemonBlockModel::blockImage(int block) const {
    if (!stroke_.contains(block)) {
        switch (kind_) {
        case Kind::Small: return session_->painter().smallBlock(world_, block, preset_);
        case Kind::Big: return session_->painter().bigBlock(world_, block, preset_);
        default: return session_->painter().metatile(block, preset_);
        }
    }
    // A block the stroke is painting.
    const std::array<int, 4> q = quarters(block);
    const int size = cellSize().width();
    QImage image(size * 2, size * 2, QImage::Format_ARGB32);
    QPainter painter(&image);
    const QJsonObject r = session_->resource(resourceId());
    const int row = kind_ == Kind::Metatile
                        ? r.value(QStringLiteral("palettes")).toArray().at(block).toInt()
                        : r.value(QStringLiteral("attributes")).toArray().at(block).toObject().value(QStringLiteral("palette")).toInt();
    for (int i = 0; i < 4; ++i) {
        const QImage piece = kind_ == Kind::Big
                                 ? session_->painter().smallBlock(world_, q[i], preset_)
                                 : session_->painter().tile(world_, q[i], session_->painter().colours(world_, preset_, row & 3));
        painter.drawImage((i % 2) * size, (i / 2) * size, piece);
    }
    return image;
}

QVector<sdk::Tool> DoraemonBlockModel::tools() const {
    const QString piece = kind_ == Kind::Big ? tr("small block") : QStringLiteral("pattern");
    return {{QStringLiteral("select"), tr("Select a block"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's %1 into quarters (right button takes one)").arg(piece),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a quarter's %1 as the brush").arg(piece), QStringLiteral("pick")}};
}

QVector<sdk::ViewOption> DoraemonBlockModel::viewOptions() const {
    sdk::ViewOption preset;
    preset.key = QStringLiteral("preset");
    preset.label = tr("Colours");
    for (int p = 0; p < paletteIds(session_->bundle(), world_).size(); ++p) preset.choices << tr("Preset %1").arg(p);
    preset.value = tr("Preset %1").arg(preset_);
    return {preset};
}

void DoraemonBlockModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("preset")) return;
    const int n = labelNumber(value, tr("Preset %1"), 64);
    if (n < 0) return;
    preset_ = n;
    emit toolsChanged();
    emit contentChanged();
    emit selectionChanged();
}

QImage DoraemonBlockModel::render() const {
    const QSize grid = gridSize();
    const int size = cellSize().width() * 2;
    QImage image(grid.width() * cellSize().width(), grid.height() * cellSize().height(), QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    QPainter painter(&image);
    for (int b = 0; b < count(); ++b) painter.drawImage((b % kPerRow) * size, (b / kPerRow) * size, blockImage(b));
    return image;
}

QVector<sdk::CellBrush> DoraemonBlockModel::brushes() const {
    return {{piece_, (kind_ == Kind::Big ? tr("Small block %1") : tr("Pattern %1")).arg(nes::hex(piece_)),
             QColor()}};
}

void DoraemonBlockModel::setCurrentBrush(int value) {
    piece_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString DoraemonBlockModel::currentLibraryItem() const {
    return QStringLiteral("piece:%1").arg(piece_);
}

void DoraemonBlockModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("piece:"))) return;
    const int piece = std::clamp(id.mid(6).toInt(), 0, 255);
    if (piece != piece_) {
        piece_ = piece;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int DoraemonBlockModel::cellValue(QPoint cell) const {
    const std::array<int, 2> at = blockAt(cell);
    return at[0] < 0 ? -1 : quarters(at[0])[static_cast<std::size_t>(at[1])];
}

bool DoraemonBlockModel::isCellEditable(QPoint cell) const {
    return blockAt(cell)[0] >= 0;
}

bool DoraemonBlockModel::commitQuarters(const QHash<int, std::array<int, 4>>& blocks, const QString& text, QString* error) {
    const QString id = resourceId();
    const QString key = listKey();
    return session_->editBundle(
        text,
        [id, key, blocks](nes::Bundle& b) {
            QJsonObject r = b.resource(id);
            QJsonArray list = r.value(key).toArray();
            for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
                list.replace(it.key(), QJsonArray{it.value()[0], it.value()[1], it.value()[2], it.value()[3]});
            }
            r.insert(key, list);
            b.setResource(id, r);
        },
        error);
}

bool DoraemonBlockModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const std::array<int, 2> at = blockAt(cell);
    if (!libraryItem.startsWith(QLatin1String("piece:")) || at[0] < 0) return false;
    setCurrentLibraryItem(libraryItem);
    std::array<int, 4> q = quarters(at[0]);
    q[static_cast<std::size_t>(at[1])] = piece_;
    selectObject(QStringLiteral("block:%1").arg(at[0]));
    return commitQuarters({{at[0], q}}, tr("Set a quarter of %1 %2").arg(title()).arg(nes::hex(at[0])), error);
}

void DoraemonBlockModel::beginStroke() {
    stroke_.clear();
}

bool DoraemonBlockModel::paintCell(QPoint cell, int value) {
    const std::array<int, 2> at = blockAt(cell);
    if (at[0] < 0 || value < 0 || value > 255) return false;
    std::array<int, 4> q = quarters(at[0]);
    if (q[static_cast<std::size_t>(at[1])] == value) return false;
    q[static_cast<std::size_t>(at[1])] = value;
    stroke_.insert(at[0], q);
    emit contentChanged();
    return true;
}

void DoraemonBlockModel::endStroke() {
    if (stroke_.isEmpty()) return;
    const QHash<int, std::array<int, 4>> painted = stroke_;
    stroke_.clear();
    QString error;
    if (!commitQuarters(painted, tr("Paint %1").arg(title()), &error)) {
        session_->report(sdk::Severity::Error, QStringLiteral("metatiles"), error);
    }
    emit contentChanged();
}

QVector<sdk::MapObject> DoraemonBlockModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int b = 0; b < count(); ++b) {
        sdk::MapObject o;
        o.id = QStringLiteral("block:%1").arg(b);
        o.label = nes::hex(b);
        o.cells = QRect((b % kPerRow) * 2, (b / kPerRow) * 2, 2, 2);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString DoraemonBlockModel::selectedObject() const {
    return selected_ >= 0 ? QStringLiteral("block:%1").arg(selected_) : QString();
}

void DoraemonBlockModel::selectObject(const QString& id) {
    const int block = id.startsWith(QLatin1String("block:")) ? id.mid(6).toInt() : -1;
    if (block != selected_) {
        selected_ = block;
        emit selectionChanged();
    }
}

bool DoraemonBlockModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0) return TileMapModel::setField(key, value, error);
    const int block = selected_;
    if (key.startsWith(QLatin1String("q"))) {
        const int quarter = key.mid(1).toInt();
        std::array<int, 4> q = quarters(block);
        q[static_cast<std::size_t>(std::clamp(quarter, 0, 3))] = std::clamp(value.toInt(), 0, 255);
        return commitQuarters({{block, q}}, tr("Edit %1 %2").arg(title()).arg(nes::hex(block)), error);
    }
    const QString id = resourceId();
    if (key == QLatin1String("palette") || key == QLatin1String("properties") || key == QLatin1String("solid")) {
        const int row = std::max(0, labelNumber(value.toString(), tr("BG %1"), 4));
        const Kind kind = kind_;
        return session_->editBundle(
            tr("Edit %1 %2").arg(title()).arg(nes::hex(block)),
            [id, kind, key, block, row, value](nes::Bundle& b) {
                QJsonObject r = b.resource(id);
                if (kind == Kind::Metatile) {
                    const QString list = key == QLatin1String("solid") ? QStringLiteral("solid") : QStringLiteral("palettes");
                    QJsonArray values = r.value(list).toArray();
                    values.replace(block, key == QLatin1String("solid") ? QJsonValue(value.toString() == QLatin1String("Solid"))
                                                                         : QJsonValue(std::clamp(row, 0, 3)));
                    r.insert(list, values);
                } else {
                    QJsonArray attributes = r.value(QStringLiteral("attributes")).toArray();
                    QJsonObject a = attributes.at(block).toObject();
                    if (key == QLatin1String("palette")) a.insert(QStringLiteral("palette"), std::clamp(row, 0, 3));
                    else a.insert(QStringLiteral("properties"), std::clamp(value.toInt(), 0, 63));
                    attributes.replace(block, a);
                    r.insert(QStringLiteral("attributes"), attributes);
                }
                b.setResource(id, r);
            },
            error);
    }
    return TileMapModel::setField(key, value, error);
}

sdk::Library DoraemonBlockModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = kind_ == Kind::Big ? tr("click: brush · drag a small block onto a quarter")
                                     : tr("click: brush · drag a pattern onto a quarter");
    const int count = kind_ == Kind::Big ? session_->resource(resourceId()).value(QStringLiteral("small_blocks")).toArray().size() : 256;
    for (int piece = 0; piece < count; ++piece) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("piece:%1").arg(piece);
        item.label = nes::hex(piece);
        item.icon = pieceImage(piece);
        result.items << item;
    }
    return result;
}

sdk::Budget DoraemonBlockModel::budget() const {
    sdk::Budget budget;
    budget.text = tr("%n blocks", nullptr, count());
    return budget;
}

sdk::Inspection DoraemonBlockModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = kind_ == Kind::Metatile ? QStringLiteral("doraemon.world2-metatiles.v1")
                                                   : QStringLiteral("doraemon.hierarchy.v1");
    if (selected_ < 0) {
        inspection.kicker = tr("Metatiles");
        inspection.title = title();
        switch (kind_) {
        case Kind::Small:
            inspection.note = tr("16x16 blocks of four background patterns. A block's attribute byte gives "
                                 "its palette row and property bits (what the player can walk through). "
                                 "Big blocks are built from these; maps are built from big blocks.");
            break;
        case Kind::Big:
            inspection.note = tr("32x32 blocks of four small blocks: the cells of the World %1 maps.")
                                  .arg(int(world_));
            break;
        default:
            inspection.note = tr("16x16 metatiles the World 2 screen streams expand their literals into: "
                                 "four patterns, a palette row and whether the ship collides with it.");
        }
        return inspection;
    }
    const std::array<int, 4> q = quarters(selected_);
    inspection.kicker = kind_ == Kind::Big ? tr("Big block") : kind_ == Kind::Small ? tr("Small block")
                                                                                                : tr("Metatile");
    inspection.title = QStringLiteral("%1 %2").arg(inspection.kicker, nes::hex(selected_));
    for (int i = 0; i < 4; ++i) {
        sdk::Field f;
        f.key = QStringLiteral("q%1").arg(i);
        f.label = QString::fromLatin1(kQuarters[i]);
        f.label[0] = f.label.at(0).toUpper();
        f.type = sdk::Field::Type::Integer;
        f.value = q[static_cast<std::size_t>(i)];
        f.minimum = 0;
        f.maximum = 255;
        f.hint = nes::hex(q[static_cast<std::size_t>(i)]);
        f.monospace = true;
        inspection.fields << f;
    }
    const QJsonObject r = session_->resource(resourceId());
    if (kind_ != Kind::Big) {
        const int row = kind_ == Kind::Metatile
                            ? r.value(QStringLiteral("palettes")).toArray().at(selected_).toInt()
                            : r.value(QStringLiteral("attributes")).toArray().at(selected_).toObject().value(QStringLiteral("palette")).toInt();
        sdk::Field palette;
        palette.key = QStringLiteral("palette");
        palette.label = tr("Palette");
        palette.type = sdk::Field::Type::Choice;
        for (int p = 0; p < 4; ++p) {
            palette.choices << tr("BG %1").arg(p);
            const QVector<int> c = session_->painter().colours(world_, preset_, p);
            QImage icon(16, 16, QImage::Format_ARGB32);
            QPainter painter(&icon);
            for (int i = 0; i < 4; ++i) painter.drawImage((i % 2) * 8, (i / 2) * 8, session_->painter().tile(world_, q[static_cast<std::size_t>(i)], c));
            painter.end();
            palette.choiceIcons << icon;
        }
        palette.value = tr("BG %1").arg(row & 3);
        inspection.fields << palette;
    }
    if (kind_ == Kind::Small) {
        sdk::Field properties;
        properties.key = QStringLiteral("properties");
        properties.label = tr("Properties");
        properties.type = sdk::Field::Type::Integer;
        properties.value = r.value(QStringLiteral("attributes")).toArray().at(selected_).toObject().value(QStringLiteral("properties")).toInt();
        properties.minimum = 0;
        properties.maximum = 63;
        properties.hint = tr("attribute bits 2-7");
        inspection.fields << properties;
    }
    if (kind_ == Kind::Metatile) {
        sdk::Field solid;
        solid.key = QStringLiteral("solid");
        solid.label = tr("Collision");
        solid.type = sdk::Field::Type::Choice;
        solid.choices = QStringList{tr("Open"), QStringLiteral("Solid")};
        solid.value = r.value(QStringLiteral("solid")).toArray().at(selected_).toBool() ? QStringLiteral("Solid") : tr("Open");
        inspection.fields << solid;
    }
    inspection.note = tr("Every map cell that uses this block changes with it.");
    return inspection;
}

QString DoraemonBlockModel::statusText() const {
    if (selected_ < 0) return tr("Metatiles · %1").arg(title());
    return tr("Metatiles · %1 · %2").arg(title(), nes::hex(selected_));
}

}  // namespace rl::doraemon
