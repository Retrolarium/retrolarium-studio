#include "alien_soldier/plugin/screens_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"
#include "megadrive/vdp.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

ScreensModel::ScreensModel(AlienSoldierSession* session, const QString& id)
    : TileMapModel(session), session_(session), id_(id) {
    const Scene& scene = session_->screen(index());
    plane_ = scene.planes[0].present ? 0 : 1;
    // A credits scene's brush starts as its first drawn word, so the tile
    // sheet shows the line its colours are on.
    for (int cell = 0; scene.nameTable >= 0 && cell < 64 * scene.nameTableRows; ++cell) {
        const int word = nameTableWord(scene, QPoint(cell % 64, cell / 64));
        if (word & 0x7FF) {
            brush_ = word;
            break;
        }
    }
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Screens || doc == Doc::Maps || doc == Doc::Tiles || doc == Doc::Compressed ||
            doc == Doc::Palettes) {
            emit contentChanged();
        }
    });
}

QString ScreensModel::screenTitle(const QString& id) {
    if (id == QLatin1String("title")) return rlTr("alien_soldier", "Title");
    if (id == QLatin1String("options")) return rlTr("alien_soldier", "Options");
    if (id == QLatin1String("story")) return rlTr("alien_soldier", "Story");
    // The options and password screens load the same maps and art; the
    // password panel is drawn on plane A, the options' menu as text.
    if (id == QLatin1String("password")) return rlTr("alien_soldier", "Options and password");
    if (id == QLatin1String("weapon-setup")) return rlTr("alien_soldier", "Weapon setup");
    if (id == QLatin1String("credits-intro")) return rlTr("alien_soldier", "Credits opening");
    if (id == QLatin1String("ending")) return rlTr("alien_soldier", "Ending");
    if (id.startsWith(QLatin1String("credits-"))) return rlTr("alien_soldier", "Credits scene %1").arg(id.mid(8));
    return id;
}

bool ScreensModel::isTable() const { return session_->screen(index()).nameTable >= 0; }

QString ScreensModel::tableSource() const {
    return session_->doc(Doc::Screens)
        .value(QStringLiteral("screens"))
        .toArray()
        .at(index())
        .toObject()
        .value(QStringLiteral("name_table"))
        .toObject()
        .value(QStringLiteral("source"))
        .toString();
}

QSize ScreensModel::gridSize() const {
    if (isTable()) return {64, std::max(1, session_->screen(index()).nameTableRows)};
    return {40, 28};
}

int ScreensModel::index() const {
    const QJsonArray screens = session_->doc(Doc::Screens).value(QStringLiteral("screens")).toArray();
    for (int at = 0; at < screens.size(); ++at) {
        if (screens.at(at).toObject().value(QStringLiteral("id")).toString() == id_) return at;
    }
    return 0;
}

QVector<sdk::MapLayer> ScreensModel::layers() const {
    QVector<sdk::MapLayer> result;
    const Scene& scene = session_->screen(index());
    if (scene.nameTable >= 0) {
        result.append(scene.nameTable == 0xE000 ? sdk::MapLayer{QStringLiteral("plane-b"), tr("Plane B")}
                                                : sdk::MapLayer{QStringLiteral("plane-a"), tr("Plane A")});
        return result;
    }
    if (scene.planes[0].present) result.append({QStringLiteral("plane-a"), tr("Plane A")});
    if (scene.planes[1].present) result.append({QStringLiteral("plane-b"), tr("Plane B")});
    return result;
}

QString ScreensModel::currentLayer() const {
    return plane_ == 0 ? QStringLiteral("plane-a") : QStringLiteral("plane-b");
}

void ScreensModel::setCurrentLayer(const QString& id) {
    plane_ = id == QLatin1String("plane-b") ? 1 : 0;
    emit toolsChanged();
    emit contentChanged();
}

QVector<sdk::Tool> ScreensModel::tools() const {
    return {{QStringLiteral("paint"), tr("Paint tiles (right button picks)"), QStringLiteral("pencil")},
            {QStringLiteral("pick"), tr("Pick a tile"), QStringLiteral("pick")}};
}

QImage ScreensModel::render() const {
    const Scene& scene = session_->screen(index());
    return scene.nameTable >= 0 ? renderNameTable(scene) : renderScene(scene, QRect(0, 0, 320, 224));
}

int ScreensModel::wordOffset(QPoint cell) const {
    const Scene& scene = session_->screen(index());
    const Plane& plane = scene.planes[plane_];
    if (!plane.present) return -1;
    const int x = ((cell.x() * 8) >> plane.shift) + plane.origin.x();
    const int y = cell.y() * 8 + plane.origin.y();
    return scene.defined(plane_, x, y) ? scene.patternOffset(plane_, x, y) : -1;
}

int ScreensModel::cellValue(QPoint cell) const {
    if (isTable()) return nameTableWord(session_->screen(index()), cell);
    const int offset = wordOffset(cell);
    const Scene& scene = session_->screen(index());
    if (offset < 0 || offset + 1 >= scene.ram.size()) return 0;
    return (quint8(scene.ram.at(offset)) << 8) | quint8(scene.ram.at(offset + 1));
}

bool ScreensModel::isCellEditable(QPoint cell) const {
    if (isTable()) return QRect(QPoint(0, 0), gridSize()).contains(cell);
    const int offset = wordOffset(cell);
    return offset >= 0 &&
           mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->screen(index()), offset).index >=
               0;
}

QString ScreensModel::lockReason(QPoint cell) const {
    return wordOffset(cell) < 0 ? tr("The plane shows nothing here.") : tr("No map document holds this tile.");
}

QVector<sdk::CellBrush> ScreensModel::brushes() const {
    const megadrive::TileWord word = megadrive::decodeTileWord(brush_);
    return {{brush_, tr("Tile %1 · line %2").arg(word.pattern, 3, 16, QLatin1Char('0')).arg(word.palette), QColor()}};
}

void ScreensModel::setCurrentBrush(int value) {
    brush_ = value & 0xFFFF;
    emit toolsChanged();
    emit selectionChanged();
}

sdk::Library ScreensModel::sheet() const {
    sdk::Library library;
    const Scene& scene = session_->screen(index());
    const int line = megadrive::decodeTileWord(brush_).palette;
    library.hint = tr("pick a tile; the brush keeps its line and flips");
    library.grid = true;
    library.columns = 32;
    library.cell = QSize(8, 8);
    for (int tile = 0; tile < 0x800; ++tile) {
        sdk::LibraryItem item;
        item.id = QString::number(tile);
        item.icon = tileImage(scene.vram, tile, scene.cram, line);
        library.items.append(item);
    }
    return library;
}

void ScreensModel::setCurrentLibraryItem(const QString& id) {
    setCurrentBrush((brush_ & ~0x7FF) | (id.toInt() & 0x7FF));
}

QString ScreensModel::currentLibraryItem() const { return QString::number(brush_ & 0x7FF); }

void ScreensModel::beginStroke() {
    beforeMaps_ = session_->doc(Doc::Maps);
    beforeScreens_ = session_->doc(Doc::Screens);
    beforeCompressed_ = session_->doc(Doc::Compressed);
    revisionMaps_ = session_->revision(Doc::Maps);
    revisionScreens_ = session_->revision(Doc::Screens);
    revisionCompressed_ = session_->revision(Doc::Compressed);
}

bool ScreensModel::paintCell(QPoint cell, int value) {
    if (isTable()) {
        // The word sits in the compressed payload's bytes, four hex digits
        // a word, sixteen words to a 32-byte "tile" of the document.
        if (!isCellEditable(cell)) return false;
        if (cellValue(cell) == (value & 0xFFFF)) return true;
        QJsonObject& document = session_->live(Doc::Compressed);
        QJsonArray payloads = document.value(QStringLiteral("payloads")).toArray();
        const int at = indexOfLabel(payloads, tableSource());
        if (at < 0) return false;
        QJsonObject payload = payloads.at(at).toObject();
        QJsonArray tiles = payload.value(QStringLiteral("tiles")).toArray();
        const int word = cell.y() * 64 + cell.x();
        QString hex = tiles.at(word / 16).toString();
        if (hex.size() < 64) return false;
        hex.replace((word % 16) * 4, 4, QStringLiteral("%1").arg(value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper());
        tiles.replace(word / 16, hex);
        payload.insert(QStringLiteral("tiles"), tiles);
        payloads.replace(at, payload);
        document.insert(QStringLiteral("payloads"), payloads);
        session_->touch(Doc::Compressed);
        return true;
    }
    const int offset = wordOffset(cell);
    if (offset < 0) return false;
    if (cellValue(cell) == value) return true;
    const Scene& scene = session_->screen(index());
    const MapValue target = mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), scene, offset);
    if (target.index < 0) return false;
    // A title word the game rewrites after loading is stored as the word
    // that rewrites to the one painted.
    int stored = value & 0xFFFF;
    for (const RamTransform& transform : scene.transforms) {
        if (offset >= transform.offset && offset < transform.offset + transform.words * 2) {
            stored = untransform(transform, stored);
            if (stored < 0) return false;
        }
    }
    try {
        setMapValue(session_->live(target.doc), target, stored);
    } catch (const std::exception&) {
        return false;
    }
    session_->touch(target.doc);
    return true;
}

void ScreensModel::endStroke() {
    session_->commit(Doc::Screens, beforeScreens_, revisionScreens_, tr("Paint screen"));
    session_->commit(Doc::Maps, beforeMaps_, revisionMaps_, tr("Paint screen"));
    session_->commit(Doc::Compressed, beforeCompressed_, revisionCompressed_, tr("Paint screen"));
}

QVector<sdk::ViewOption> ScreensModel::viewOptions() const { return {}; }

void ScreensModel::setViewOption(const QString& key, const QString& value) {
    Q_UNUSED(key);
    Q_UNUSED(value);
}

QString ScreensModel::statusText() const {
    const megadrive::TileWord word = megadrive::decodeTileWord(brush_);
    return tr("%1 · brush tile %2, line %3").arg(title()).arg(word.pattern, 3, 16, QLatin1Char('0')).arg(word.palette);
}

sdk::Inspection ScreensModel::inspect() const {
    sdk::Inspection inspection;
    const megadrive::TileWord word = megadrive::decodeTileWord(brush_);
    inspection.kicker = tr("Brush");
    inspection.title = title();
    const auto integer = [](const QString& key, const QString& label, int value, int maximum) {
        sdk::Field field;
        field.key = key;
        field.label = label;
        field.type = sdk::Field::Type::Integer;
        field.value = value;
        field.maximum = maximum;
        field.monospace = true;
        return field;
    };
    inspection.fields = {integer(QStringLiteral("tile"), tr("Tile"), word.pattern, 0x7FF),
                         integer(QStringLiteral("line"), tr("Palette line"), word.palette, 3),
                         integer(QStringLiteral("hflip"), tr("Flip across"), word.hflip, 1),
                         integer(QStringLiteral("vflip"), tr("Flip down"), word.vflip, 1),
                         integer(QStringLiteral("priority"), tr("In front"), word.priority, 1)};
    inspection.note =
        tr("Two routines adjust the title's tiles after loading; the screen shows them adjusted, and "
           "painting writes the word the map holds.");
    return inspection;
}

bool ScreensModel::setField(const QString& key, const QVariant& value, QString* error) {
    megadrive::TileWord word = megadrive::decodeTileWord(brush_);
    const int number = value.toInt();
    if (key == QLatin1String("tile")) word.pattern = number & 0x7FF;
    else if (key == QLatin1String("line")) word.palette = std::clamp(number, 0, 3);
    else if (key == QLatin1String("hflip")) word.hflip = number != 0;
    else if (key == QLatin1String("vflip")) word.vflip = number != 0;
    else if (key == QLatin1String("priority")) word.priority = number != 0;
    else {
        if (error) *error = tr("Unknown field.");
        return false;
    }
    setCurrentBrush(megadrive::encodeTileWord(word));
    return true;
}

}  // namespace rl::alien_soldier
