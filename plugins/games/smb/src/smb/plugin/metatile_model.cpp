#include "smb/plugin/metatile_model.hpp"

#include "smb/plugin/smb_session.hpp"

#include <QPainter>

namespace rl::smb {

namespace {

constexpr int kBlankTile = 0x24;

QString hex(int value, int digits = 2) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QString packetLabel(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

const char* const kQuadrants[4] = {"Top left", "Top right", "Bottom left", "Bottom right"};

}  // namespace

SmbMetatileModel::SmbMetatileModel(SmbSession* session, int group)
    : TileMapModel(session), session_(session), group_(group) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
    packet_ = QStringLiteral("ground");
    const QStringList packets = session_->document().packetNames(session_->palettes());
    if (!packets.contains(packet_) && !packets.isEmpty()) {
        packet_ = packets.first();
    }
}

int SmbMetatileModel::count() const {
    return session_->document().metatileGroupSizes(session_->metatiles()).value(group_);
}

QString SmbMetatileModel::title() const { return tr("Palette %1 blocks").arg(group_); }

QSize SmbMetatileModel::gridSize() const { return {kPerRow * 2, std::max(1, (count() + kPerRow - 1) / kPerRow) * 2}; }

int SmbMetatileModel::tileOf(const SmbResourceDocument& document, int index, int quadrant) const {
    const auto found = pending_.constFind(index);
    if (found != pending_.constEnd()) {
        return found->at(quadrant);
    }
    const int base = document.resource(session_->metatiles()).value(QStringLiteral("tile_base")).toInt();
    return document.metatileTiles(session_->metatiles(), encoded(index)).at(quadrant) - base;
}

QPair<int, int> SmbMetatileModel::blockAt(QPoint cell) const {
    const int index = (cell.y() / 2) * kPerRow + cell.x() / 2;
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= kPerRow * 2 || index >= count()) {
        return {-1, -1};
    }
    return {index, (cell.y() % 2) * 2 + cell.x() % 2};
}

// ---------------------------------------------------------------- view

QVector<sdk::Tool> SmbMetatileModel::tools() const {
    return {{QStringLiteral("select"), tr("Select a block"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's pattern into quadrants (right button takes one)"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a quadrant's pattern as the brush"), QStringLiteral("pick")}};
}

QVector<sdk::ViewOption> SmbMetatileModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Colours");
    palette.value = packetLabel(packet_);
    for (const QString& name : session_->document().packetNames(session_->palettes())) {
        if (session_->document().packetValues(session_->palettes(), name).size() >= 16) {
            palette.choices << packetLabel(name);
        }
    }
    return {palette};
}

void SmbMetatileModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        for (const QString& name : session_->document().packetNames(session_->palettes())) {
            if (packetLabel(name) == value) {
                packet_ = name;
            }
        }
        emit contentChanged();
        emit selectionChanged();
    }
}

QImage SmbMetatileModel::render() const {
    const QSize grid = gridSize();
    QImage image(grid.width() * 8, grid.height() * 8, QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    QPainter painter(&image);
    const int backdrop = session_->document().dayBackground(session_->palettes());
    for (int i = 0; i < count(); ++i) {
        const QPoint at((i % kPerRow) * 16, (i / kPerRow) * 16);
        const auto found = pending_.constFind(i);
        if (found == pending_.constEnd()) {
            painter.drawImage(at, session_->painter().metatile(encoded(i), packet_, false));
            continue;
        }
        // A block the stroke is painting: its patterns as they will be.
        for (int q = 0; q < 4; ++q) {
            painter.drawImage(at + QPoint((q % 2) * 8, (q / 2) * 8),
                              session_->painter().backgroundTile(found->at(q), packet_, group_, backdrop));
        }
    }
    return image;
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> SmbMetatileModel::brushes() const {
    return {{kBlankTile, tr("Blank"), QColor()}, {tile_, tr("Tile %1").arg(hex(tile_)), QColor()}};
}

void SmbMetatileModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString SmbMetatileModel::currentLibraryItem() const { return QStringLiteral("tile:%1").arg(tile_); }

void SmbMetatileModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) {
        return;
    }
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int SmbMetatileModel::cellValue(QPoint cell) const {
    const QPair<int, int> at = blockAt(cell);
    return at.first < 0 ? -1 : tileOf(session_->document(), at.first, at.second);
}

bool SmbMetatileModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const QPair<int, int> at = blockAt(cell);
    if (!libraryItem.startsWith(QLatin1String("tile:")) || at.first < 0) {
        if (error) *error = tr("Drop a pattern onto a quadrant of a block.");
        return false;
    }
    const int tile = libraryItem.mid(5).toInt();
    setCurrentLibraryItem(libraryItem);
    const QString metatiles = session_->metatiles();
    const int block = encoded(at.first);
    const int quadrant = at.second;
    selectObject(QStringLiteral("block:%1").arg(at.first));
    return session_->edit(
        tr("Set a pattern of block %1").arg(hex(block)),
        [metatiles, block, quadrant, tile](SmbResourceDocument& d) {
            d.setMetatileTile(metatiles, block, quadrant, tile);
        },
        error);
}

bool SmbMetatileModel::isCellEditable(QPoint cell) const { return blockAt(cell).first >= 0; }

void SmbMetatileModel::beginStroke() {
    stroking_ = true;
    pending_.clear();
}

bool SmbMetatileModel::paintCell(QPoint cell, int value) {
    const QPair<int, int> at = blockAt(cell);
    if (!stroking_ || at.first < 0 || value < 0 || value > 255 ||
        tileOf(session_->document(), at.first, at.second) == value) {
        return false;
    }
    auto found = pending_.find(at.first);
    if (found == pending_.end()) {
        std::array<int, 4> tiles{};
        for (int q = 0; q < 4; ++q) tiles[q] = tileOf(session_->document(), at.first, q);
        found = pending_.insert(at.first, tiles);
    }
    (*found)[at.second] = value;
    emit contentChanged();
    return true;
}

void SmbMetatileModel::endStroke() {
    stroking_ = false;
    if (pending_.isEmpty()) {
        return;
    }
    const QHash<int, std::array<int, 4>> pending = pending_;
    const QString metatiles = session_->metatiles();
    const int group = group_;
    // Cleared first: the edit redraws from the document.
    pending_.clear();
    QString error;
    session_->edit(
        tr("Paint %1").arg(title()),
        [pending, metatiles, group](SmbResourceDocument& d) {
            for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
                for (int q = 0; q < 4; ++q) {
                    d.setMetatileTile(metatiles, (group << 6) | it.key(), q, it.value()[q]);
                }
            }
        },
        &error);
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> SmbMetatileModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int i = 0; i < count(); ++i) {
        sdk::MapObject o;
        o.id = QStringLiteral("block:%1").arg(i);
        o.label = hex(encoded(i));
        o.cells = QRect((i % kPerRow) * 2, (i / kPerRow) * 2, 2, 2);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString SmbMetatileModel::selectedObject() const {
    return selected_ >= 0 ? QStringLiteral("block:%1").arg(selected_) : QString();
}

void SmbMetatileModel::selectObject(const QString& id) {
    const int block = id.startsWith(QLatin1String("block:")) ? id.mid(6).toInt() : -1;
    if (block != selected_) {
        selected_ = block;
        emit selectionChanged();
    }
}

// ---------------------------------------------------------------- inspector

bool SmbMetatileModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ >= 0 && key == QLatin1String("copy")) {
        // "$45" (any group): take all four patterns of that block.
        bool ok = false;
        const int source = value.toString().mid(1).toInt(&ok, 16);
        if (!ok) {
            return true;
        }
        const QString metatiles = session_->metatiles();
        const int target = encoded(selected_);
        const int base = session_->document().resource(metatiles).value(QStringLiteral("tile_base")).toInt();
        const std::array<int, 4> from = session_->document().metatileTiles(metatiles, source);
        return session_->edit(
            tr("Copy block %1 into %2").arg(hex(source), hex(target)),
            [metatiles, target, from, base](SmbResourceDocument& d) {
                for (int q = 0; q < 4; ++q) d.setMetatileTile(metatiles, target, q, from[q] - base);
            },
            error);
    }
    if (selected_ < 0 || !key.startsWith(QLatin1Char('q'))) {
        return TileMapModel::setField(key, value, error);
    }
    const int quadrant = key.mid(1).toInt();
    const int tile = value.toInt();
    const QString metatiles = session_->metatiles();
    const int block = encoded(selected_);
    return session_->edit(
        tr("Set a pattern of block %1").arg(hex(block)),
        [metatiles, block, quadrant, tile](SmbResourceDocument& d) {
            d.setMetatileTile(metatiles, block, quadrant, tile);
        },
        error);
}

sdk::Library SmbMetatileModel::sheet() const { return library(); }

sdk::Library SmbMetatileModel::library() const {
    const int backdrop = session_->document().dayBackground(session_->palettes());
    const QString key = QStringLiteral("%1|%2").arg(session_->revision()).arg(packet_);
    if (key != libraryKey_) {
        library_.clear();
        for (int tile = 0; tile < 256; ++tile) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = hex(tile);
            item.icon = session_->painter().backgroundTile(tile, packet_, group_, backdrop);
            library_ << item;
        }
        libraryKey_ = key;
    }
    sdk::Library result;
    result.grid = true;
    result.hint = tr("click: brush · drag onto a quadrant");
    result.items = library_;
    return result;
}

sdk::Inspection SmbMetatileModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("nes.metatile-2x2.v1");
    if (selected_ < 0 || selected_ >= count()) {
        inspection.kicker = tr("Metatiles");
        inspection.title = title();
        sdk::Field blocks;
        blocks.key = QStringLiteral("blocks");
        blocks.label = tr("Blocks");
        blocks.value = QStringLiteral("%1 · %2-%3").arg(count()).arg(hex(encoded(0))).arg(hex(encoded(count() - 1)));
        blocks.monospace = true;
        inspection.fields = {blocks};
        inspection.note = tr("Levels are built from these 16x16 blocks; the group picks background "
                             "palette %1. Changing a block changes it everywhere the game draws it. "
                             "Pick a pattern in the Patterns sheet and paint quadrants, or select a block.")
                              .arg(group_);
        return inspection;
    }
    inspection.kicker = tr("Metatile");
    inspection.title = tr("Block %1").arg(hex(encoded(selected_)));
    for (int q = 0; q < 4; ++q) {
        const int tile = tileOf(session_->document(), selected_, q);
        sdk::Field f;
        f.key = QStringLiteral("q%1").arg(q);
        f.label = QString::fromLatin1(kQuadrants[q]);
        f.type = sdk::Field::Type::Integer;
        f.value = tile;
        f.minimum = 0;
        f.maximum = 255;
        f.hint = hex(tile);
        f.monospace = true;
        inspection.fields << f;
    }
    // Rebuild from another block: every block of every group, as it looks.
    sdk::Field copy;
    copy.key = QStringLiteral("copy");
    copy.label = tr("Copy from");
    copy.type = sdk::Field::Type::Choice;
    copy.choices << QStringLiteral("—");
    copy.choiceIcons << QImage();
    const QVector<int> sizes = session_->document().metatileGroupSizes(session_->metatiles());
    for (int g = 0; g < sizes.size(); ++g) {
        for (int i = 0; i < sizes.at(g); ++i) {
            const int value = (g << 6) | i;
            if (value == encoded(selected_)) continue;
            copy.choices << hex(value);
            copy.choiceIcons << session_->painter().metatile(value, packet_, false);
        }
    }
    copy.value = QStringLiteral("—");
    inspection.fields << copy;
    inspection.note = tr("Four background patterns in palette %1. Paint quadrants with a pattern from "
                         "the Patterns sheet, take one from any block with the pick tool, or copy a whole block. "
                         "Every level that places block %2 changes with it.")
                          .arg(group_)
                          .arg(hex(encoded(selected_)));
    inspection.engine = {{tr("Encoded"), hex(encoded(selected_))},
                         {tr("Group"), tr("%1 · index %2").arg(group_).arg(selected_)}};
    return inspection;
}

sdk::Budget SmbMetatileModel::budget() const {
    sdk::Budget budget;
    budget.used = count();
    budget.capacity = count();
    budget.text = tr("%1 blocks · fixed").arg(count());
    return budget;
}

QString SmbMetatileModel::statusText() const {
    if (selected_ < 0) {
        return tr("%1 · %2 blocks").arg(title()).arg(count());
    }
    return tr("%1 · block %2").arg(title(), hex(encoded(selected_)));
}

}  // namespace rl::smb
