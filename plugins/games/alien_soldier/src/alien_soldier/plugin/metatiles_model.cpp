#include "alien_soldier/plugin/metatiles_model.hpp"

#include "alien_soldier/data/scene.hpp"
#include "alien_soldier/plugin/map_edits.hpp"
#include "alien_soldier/plugin/session.hpp"

#include <QPainter>

#include <algorithm>
#include <stdexcept>

namespace rl::alien_soldier {

namespace {

constexpr int kChunksPerRow = 8;

QString hex(int value) { return QStringLiteral("$%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(); }

}  // namespace

MetatilesModel::MetatilesModel(AlienSoldierSession* session, int set, bool planeB)
    : TileMapModel(session), session_(session), set_(set), plane_(planeB ? 1 : 0) {
    const QVector<BlockSet> sets = session_->blockSets();
    if (set_ >= 0 && set_ < sets.size()) stage_ = sets[set_].stages.first();
    connect(session_, &AlienSoldierSession::documentChanged, this, [this](Doc doc) {
        if (doc == Doc::Maps || doc == Doc::Tiles || doc == Doc::Compressed || doc == Doc::Palettes)
            emit contentChanged();
    });
}

QString MetatilesModel::title() const {
    return tr("%1 · %2").arg(session_->blockSetLabel(set_), plane_ ? tr("Plane B") : tr("Plane A"));
}

bool MetatilesModel::isReadOnly() const { return chunkCount(session_->stage(stage_), plane()) == 0; }

QString MetatilesModel::readOnlyReason() const { return tr("This stage slot loads no map."); }

QSize MetatilesModel::gridSize() const {
    const int chunks = std::max(1, chunkCount(session_->stage(stage_), plane()));
    return {kChunksPerRow * 8, ((chunks + kChunksPerRow - 1) / kChunksPerRow) * 8};
}

QVector<sdk::Tool> MetatilesModel::tools() const {
    if (isReadOnly()) return {};
    return {{QStringLiteral("paint"), tr("Paint patterns (right button picks)"), QStringLiteral("pencil")},
            {QStringLiteral("pick"), tr("Pick a pattern"), QStringLiteral("pick")}};
}

QImage MetatilesModel::render() const {
    const Scene& scene = session_->stage(stage_);
    const QSize size = gridSize() * 32;
    QImage image(size, QImage::Format_ARGB32);
    image.fill(QColor::fromRgb(cramColor(scene.cram.value(scene.backdrop))));
    QPainter painter(&image);
    const int chunks = chunkCount(scene, plane());
    for (int chunk = 0; chunk < chunks; ++chunk) {
        painter.drawImage((chunk % kChunksPerRow) * 256, (chunk / kChunksPerRow) * 256,
                          chunkImage(scene, plane(), chunk));
    }
    painter.setPen(QColor(255, 255, 255, 90));
    for (int x = 256; x < size.width(); x += 256) painter.drawLine(x, 0, x, size.height());
    for (int y = 256; y < size.height(); y += 256) painter.drawLine(0, y, size.width(), y);
    return image;
}

int MetatilesModel::slotOffset(QPoint cell) const {
    const Scene& scene = session_->stage(stage_);
    const int chunk = (cell.y() / 8) * kChunksPerRow + cell.x() / 8;
    if (cell.x() < 0 || cell.y() < 0 || chunk >= chunkCount(scene, plane())) return -1;
    return int(scene.planes[plane()].chunks - kRamBase) + chunk * 64 + (cell.y() % 8) * 8 + cell.x() % 8;
}

// The pattern sheet beside the chunks picks the brush; no button row.
QVector<sdk::CellBrush> MetatilesModel::brushes() const { return {}; }

void MetatilesModel::setCurrentBrush(int value) {
    brush_ = value;
    emit toolsChanged();
}

int MetatilesModel::cellValue(QPoint cell) const {
    const int offset = slotOffset(cell);
    const Scene& scene = session_->stage(stage_);
    return offset < 0 || offset >= scene.ram.size() ? 0 : quint8(scene.ram.at(offset));
}

bool MetatilesModel::isCellEditable(QPoint cell) const {
    const int offset = slotOffset(cell);
    return offset >= 0 &&
           mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->stage(stage_), offset).index >=
               0;
}

QString MetatilesModel::lockReason(QPoint cell) const {
    return slotOffset(cell) < 0 ? tr("There is no chunk here.") : tr("No map document holds this chunk.");
}

sdk::Library MetatilesModel::sheet() const {
    sdk::Library library;
    const Scene& scene = session_->stage(stage_);
    library.hint = tr("pick a pattern, then paint chunk slots");
    library.grid = true;
    library.columns = 8;
    library.cell = QSize(32, 32);  // a pattern, 4x4 tiles
    const int patterns = std::min(patternCount(scene, plane()), 256);
    for (int pattern = 0; pattern < patterns; ++pattern) {
        sdk::LibraryItem item;
        item.id = QString::number(pattern);
        item.label = hex(pattern);
        item.icon = patternImage(scene, plane(), pattern);
        library.items.append(item);
    }
    return library;
}

void MetatilesModel::setCurrentLibraryItem(const QString& id) { setCurrentBrush(id.toInt()); }

bool MetatilesModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    bool number = false;
    const int pattern = libraryItem.toInt(&number);
    if (!number) return TileMapModel::placeObject(libraryItem, cell, error);
    if (!isCellEditable(cell)) {
        if (error) *error = lockReason(cell);
        return false;
    }
    beginStroke();
    const bool painted = paintCell(cell, pattern);
    endStroke();
    setCurrentBrush(pattern);
    return painted;
}

QString MetatilesModel::currentLibraryItem() const { return QString::number(brush_); }

void MetatilesModel::beginStroke() {
    strokeBefore_ = session_->doc(Doc::Maps);
    strokeRevision_ = session_->revision(Doc::Maps);
    overflowReported_ = false;
}

bool MetatilesModel::paintCell(QPoint cell, int value) {
    const int offset = slotOffset(cell);
    if (offset < 0 || value < 0 || value > 0xFF) return false;
    if (cellValue(cell) == value) return true;
    const MapValue target =
        mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->stage(stage_), offset);
    if (target.index < 0 || target.doc != Doc::Maps) return false;
    try {
        setMapValue(session_->live(Doc::Maps), target, value);
    } catch (const MapOverflow& full) {
        session_->reportMapOverflow(QStringLiteral("metatiles"), full, &overflowReported_);
        return false;
    } catch (const std::exception&) {
        return false;
    }
    session_->touch(Doc::Maps);
    return true;
}

void MetatilesModel::endStroke() { session_->commit(Doc::Maps, strokeBefore_, strokeRevision_, tr("Paint patterns")); }

sdk::Budget MetatilesModel::budget() const {
    sdk::Budget budget;
    const int offset = slotOffset(QPoint(0, 0));
    if (offset < 0) return budget;
    const MapValue target =
        mapValueAt(session_->doc(Doc::Maps), session_->doc(Doc::Screens), session_->stage(stage_), offset);
    if (target.index < 0 || target.doc != Doc::Maps) return budget;
    const QJsonArray payloads = session_->doc(Doc::Maps).value(QStringLiteral("payloads")).toArray();
    const QJsonObject payload = payloads.at(indexOfLabel(payloads, target.payload)).toObject();
    budget.capacity = payload.value(QStringLiteral("capacity")).toInt();
    budget.used = mapPayloadSize(payload.value(QStringLiteral("values")).toArray(), target.width);
    budget.text = tr("%1 · %2 of %3 bytes compressed").arg(target.payload).arg(budget.used).arg(budget.capacity);
    return budget;
}

QString MetatilesModel::statusText() const {
    return tr("%1 chunks · %2 patterns")
        .arg(chunkCount(session_->stage(stage_), plane()))
        .arg(patternCount(session_->stage(stage_), plane()));
}

sdk::Inspection MetatilesModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Chunks");
    inspection.title = title();
    sdk::Field brush;
    brush.key = QStringLiteral("brush");
    brush.label = tr("Pattern");
    brush.value = hex(brush_);
    inspection.fields = {brush};
    inspection.note =
        tr("A chunk is 8x8 patterns and a pattern 4x4 tiles. Every stage of the level draws with these "
           "chunks; the build compresses each map again into its slot.");
    return inspection;
}

}  // namespace rl::alien_soldier
