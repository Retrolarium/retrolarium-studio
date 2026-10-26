#include "alien_soldier/plugin/object_metatiles_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/mapping_canvas.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::alien_soldier {

namespace {

constexpr int kCell = 40;  // a 32-pixel metatile and its margin
constexpr int kColumns = 8;

int widthOf(int size) { return ((size >> 2) & 3) + 1; }
int heightOf(int size) { return (size & 3) + 1; }

QStringList sizes() { return {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")}; }

}  // namespace

ObjectMetatilesModel::ObjectMetatilesModel(AlienSoldierSession* session, const QString& group)
    : CompositionModel(session), session_(session), group_(group) {
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Mappings || doc == Doc::Tiles || doc == Doc::Compressed || doc == Doc::Palettes)
            emit contentChanged();
    });
}

QString ObjectMetatilesModel::title() const {
    for (const QJsonValue& value : session_->doc(Doc::Objects).value(QStringLiteral("groups")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("id")).toString() == group_)
            return item.value(QStringLiteral("title")).toString(group_);
    }
    return group_;
}

QVector<ObjectMetatilesModel::Entry> ObjectMetatilesModel::entries() const {
    QStringList labels;
    for (const QJsonValue& value : session_->doc(Doc::Objects).value(QStringLiteral("groups")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("id")).toString() != group_) continue;
        for (const QJsonValue& label : item.value(QStringLiteral("blocks")).toArray()) labels << label.toString();
    }
    QVector<Entry> found;
    QStringList placed;
    int row = 0;
    for (const QString& label : labels) {
        if (placed.contains(label)) continue;
        // A row of metatiles per art the file's blocks draw with.
        const QStringList blocks = blocksDrawnLike(session_->docs(), label);
        placed << blocks;
        QVector<int> uses;
        const QVector<Metatile> metatiles = metatilesOf(session_->docs(), blocks, &uses);
        for (int index = 0; index < metatiles.size(); ++index) {
            found.append({metatiles[index], blocks, uses[index], QPoint(index % kColumns, row + index / kColumns)});
        }
        if (!metatiles.isEmpty()) row += (int(metatiles.size()) + kColumns - 1) / kColumns;
    }
    return found;
}

int ObjectMetatilesModel::current(const QVector<Entry>& list) const {
    if (list.isEmpty()) return -1;
    return std::clamp(selected_.isEmpty() ? 0 : selected_.toInt(), 0, int(list.size()) - 1);
}

QSize ObjectMetatilesModel::canvasSize() const {
    int rows = 1;
    for (const Entry& entry : entries()) rows = std::max(rows, entry.cell.y() + 1);
    return {kColumns * kCell, rows * kCell};
}

QImage ObjectMetatilesModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const QVector<int> cram = paletteList(session_->doc(Doc::Palettes), QString());
    QString shownBlock;
    SpriteContext context;
    for (const Entry& entry : entries()) {
        if (entry.blocks.value(0) != shownBlock) {
            shownBlock = entry.blocks.value(0);
            context = spriteContext(session_->docs(), shownBlock);
        }
        const QPoint at = entry.cell * kCell;
        painter.fillRect(QRect(at + QPoint(2, 2), QSize(kCell - 4, kCell - 4)), QColor(255, 255, 255, 16));
        painter.drawImage(at + QPoint(4, 4), metatileImage(entry.metatile, session_->docs(), context, cram, 0, 32));
    }
    return image;
}

QVector<sdk::CompositionPiece> ObjectMetatilesModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const QVector<Entry> list = entries();
    QString shownBlock;
    SpriteContext context;
    for (int index = 0; index < list.size(); ++index) {
        const Entry& entry = list[index];
        if (entry.blocks.value(0) != shownBlock) {
            shownBlock = entry.blocks.value(0);
            context = spriteContext(session_->docs(), shownBlock);
        }
        const QSize size(widthOf(entry.metatile.size) * 8, heightOf(entry.metatile.size) * 8);
        sdk::CompositionPiece piece;
        piece.id = QString::number(index);
        piece.label = metatileLabel(entry.metatile, context);
        piece.pixels =
            QRect(entry.cell * kCell + QPoint(4 + (32 - size.width()) / 2, 4 + (32 - size.height()) / 2), size);
        piece.movable = false;
        result.append(piece);
    }
    return result;
}

void ObjectMetatilesModel::selectPiece(const QString& id) {
    selected_ = id;
    emit selectionChanged();
}

QString ObjectMetatilesModel::statusText() const {
    return tr("%1 · %n metatiles", nullptr, int(entries().size())).arg(title());
}

sdk::Inspection ObjectMetatilesModel::inspect() const {
    sdk::Inspection inspection;
    const QVector<Entry> list = entries();
    if (selected_.isEmpty() || list.isEmpty()) {
        inspection.kicker = tr("Object metatiles");
        inspection.title = title();
        inspection.note =
            tr("What the pieces of this object's frames draw: a first tile, a size and a palette line. Select one to "
               "change it in every frame that uses it; Objects and Animations put them into pieces from their sheet.");
        return inspection;
    }
    const Entry& entry = list[current(list)];
    const SpriteContext context = spriteContext(session_->docs(), entry.blocks.value(0));
    const int base = context.known ? context.attribute & 0x7FF : 0;
    const int objectLine = context.known ? (context.attribute >> 13) & 3 : 0;
    inspection.kicker = tr("Object metatile");
    inspection.title = metatileLabel(entry.metatile, context);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("First tile");
    tile.type = sdk::Field::Type::Integer;
    tile.value = (entry.metatile.tile + base) & 0x7FF;
    tile.minimum = 0;
    tile.maximum = 0x7FF;
    tile.monospace = true;
    sdk::Field width;
    width.key = QStringLiteral("width");
    width.label = tr("Width");
    width.type = sdk::Field::Type::Choice;
    width.choices = sizes();
    width.value = QString::number(widthOf(entry.metatile.size));
    sdk::Field height = width;
    height.key = QStringLiteral("height");
    height.label = tr("Height");
    height.value = QString::number(heightOf(entry.metatile.size));
    sdk::Field line;
    line.key = QStringLiteral("line");
    line.label = tr("Palette line");
    line.type = sdk::Field::Type::Choice;
    line.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    line.value = QString::number(entry.metatile.line ^ objectLine);
    sdk::Field uses;
    uses.key = QStringLiteral("uses");
    uses.label = tr("Used by");
    uses.value = tr("%n pieces", nullptr, entry.uses);
    inspection.fields = {tile, width, height, line, uses};
    inspection.note = tr("Every piece of %1 that draws this metatile changes with it. Its tiles run down each column, "
                         "then right, from the first tile; the sheet on the right picks the first tile.")
                          .arg(entry.blocks.join(QStringLiteral(", ")));
    return inspection;
}

bool ObjectMetatilesModel::setField(const QString& key, const QVariant& value, QString* error) {
    const QVector<Entry> list = entries();
    if (selected_.isEmpty() || list.isEmpty()) return false;
    const Entry& entry = list[current(list)];
    const SpriteContext context = spriteContext(session_->docs(), entry.blocks.value(0));
    const int base = context.known ? context.attribute & 0x7FF : 0;
    const int objectLine = context.known ? (context.attribute >> 13) & 3 : 0;
    Metatile to = entry.metatile;
    const int number = value.toInt();
    if (key == QLatin1String("tile")) {
        to.tile = (number - base) & 0x7FF;
    } else if (key == QLatin1String("width") && number >= 1 && number <= 4) {
        to.size = (number - 1) << 2 | (to.size & 3);
    } else if (key == QLatin1String("height") && number >= 1 && number <= 4) {
        to.size = (to.size & 12) | (number - 1);
    } else if (key == QLatin1String("line") && number >= 0 && number <= 3) {
        to.line = number ^ objectLine;
    } else {
        return false;
    }
    return change(entry, to, error);
}

bool ObjectMetatilesModel::change(const Entry& entry, const Metatile& to, QString* error) {
    if (to == entry.metatile) return true;
    const bool done = session_->edit(
        Doc::Mappings, tr("Change metatile"),
        [&](QJsonObject& document) {
            QJsonArray blocks = document.value(QStringLiteral("blocks")).toArray();
            for (const QString& label : entry.blocks) {
                const int at = indexOfLabel(blocks, label);
                if (at < 0) continue;
                QJsonObject block = blocks.at(at).toObject();
                QJsonArray mappings = block.value(QStringLiteral("mappings")).toArray();
                for (int index = 0; index < mappings.size(); ++index) {
                    QJsonObject mapping = mappings.at(index).toObject();
                    QJsonArray pieces = mapping.value(QStringLiteral("pieces")).toArray();
                    for (int piece = 0; piece < pieces.size(); ++piece) {
                        Metatile drawn;
                        if (metatileOf(pieces.at(piece).toObject(), &drawn) && drawn == entry.metatile)
                            pieces.replace(piece, withMetatile(pieces.at(piece).toObject(), to));
                    }
                    mapping.insert(QStringLiteral("pieces"), pieces);
                    mappings.replace(index, mapping);
                }
                block.insert(QStringLiteral("mappings"), mappings);
                blocks.replace(at, block);
            }
            document.insert(QStringLiteral("blocks"), blocks);
        },
        error);
    if (!done) return false;
    // The selection follows the metatile (merged into another one if it now
    // draws the same).
    const QVector<Entry> list = entries();
    for (int index = 0; index < list.size(); ++index) {
        if (list[index].metatile == to && list[index].blocks == entry.blocks) {
            selectPiece(QString::number(index));
            break;
        }
    }
    return true;
}

sdk::Library ObjectMetatilesModel::sheet() const {
    sdk::Library library;
    const QVector<Entry> list = entries();
    if (list.isEmpty()) return library;
    const Entry& entry = list[current(list)];
    const QString block = entry.blocks.value(0);
    const SpriteContext context = spriteContext(session_->docs(), block);
    const QJsonObject record =
        session_->doc(Doc::Objects).value(QStringLiteral("contexts")).toObject().value(block).toObject();
    if (!context.known) return library;
    const int line = (entry.metatile.line ^ (context.attribute >> 13)) & 3;
    library.grid = true;
    library.columns = 16;
    library.cell = QSize(8, 8);
    library.hint = selected_.isEmpty() ? tr("select a metatile") : tr("click: the metatile starts at this tile");
    QVector<bool> listed(0x800, false);
    for (const QJsonValue& value : record.value(QStringLiteral("loads")).toArray()) {
        const QJsonObject load = value.toObject();
        const int first = (load.value(QStringLiteral("vram")).toInt() & 0xFFFF) / 32;
        const int count = int(vramSource(session_->docs(), load.value(QStringLiteral("command")).toInt(),
                                         load.value(QStringLiteral("source")).toString())
                                  .size()) /
                          32;
        for (int tile = first; tile < std::min(first + count, 0x800); ++tile) {
            if (listed[tile]) continue;
            listed[tile] = true;
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = QStringLiteral("$%1").arg(tile, 3, 16, QLatin1Char('0')).toUpper();
            item.icon = tileImage(context.vram, tile, context.cram, line);
            library.items.append(item);
        }
    }
    return library;
}

QString ObjectMetatilesModel::currentLibraryItem() const {
    const QVector<Entry> list = entries();
    if (selected_.isEmpty() || list.isEmpty()) return {};
    const Entry& entry = list[current(list)];
    const SpriteContext context = spriteContext(session_->docs(), entry.blocks.value(0));
    return QStringLiteral("tile:%1").arg((entry.metatile.tile + (context.attribute & 0x7FF)) & 0x7FF);
}

void ObjectMetatilesModel::setCurrentLibraryItem(const QString& id) {
    if (selected_.isEmpty() || !id.startsWith(QLatin1String("tile:")) || id == currentLibraryItem()) return;
    QString error;
    setField(QStringLiteral("tile"), id.mid(5).toInt(), &error);
}

}  // namespace rl::alien_soldier
