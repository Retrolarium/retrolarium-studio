#include "castle_excellent/plugin/sprite_model.hpp"

#include "castle_excellent/plugin/castle_excellent_session.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::castle_excellent {

namespace {

QString hex(int value) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

// The frames the game's code points at (LDX #hi / LDY #lo / JSR renderer
// between $AC60 and $B1D6; the same in both revisions): the player's two-
// sprite frames, then the objects', as first descriptor and sprite count.
// The code is not edited, so the frames stay where they are.
constexpr int kPlayerFrames = 16;
constexpr int kObjectFrames[][2] = {
    {32, 2},  {34, 2},  {36, 2},  {38, 2},  {40, 2},  {42, 2},  {44, 2},  {46, 2},  {48, 2},  {50, 2},
    {52, 2},  {54, 2},  {56, 2},  {58, 2},  {60, 1},  {61, 2},  {63, 2},  {65, 2},  {67, 2},  {69, 2},
    {71, 2},  {73, 2},  {75, 2},  {77, 2},  {79, 2},  {81, 2},  {83, 2},  {85, 2},  {87, 2},  {91, 2},
    {93, 2},  {95, 2},  {99, 2},  {101, 2}, {103, 2}, {105, 2}, {107, 2}, {109, 2}, {111, 2}, {113, 2},
    {115, 2}, {123, 2}, {125, 1}, {126, 1}, {127, 1}, {128, 1}, {129, 1}, {130, 1}, {131, 2}, {133, 2},
    {135, 2}, {137, 2}, {139, 2}, {141, 2}, {143, 2}, {145, 2}, {147, 2}, {149, 2}, {151, 2}, {153, 2},
    {155, 2}, {157, 2}, {159, 2}, {163, 2}, {165, 2}, {167, 2}, {169, 2}, {171, 2}, {173, 4}, {177, 2},
    {181, 2}, {183, 2}, {185, 2}, {187, 1}, {188, 1}, {189, 2}, {191, 2}, {193, 2}, {195, 2}, {197, 2},
    {199, 2}, {201, 2},
};
constexpr int kSheetColumns = 24;   // sprites per row, with a gap after each frame

const QStringList& flipNames() {
    static const QStringList kNames{QStringLiteral("None"), rlTr("castle_excellent", "Horizontal"), rlTr("castle_excellent", "Vertical"),
                                    rlTr("castle_excellent", "Both")};
    return kNames;
}

}  // namespace

CastleExcellentSpriteModel::CastleExcellentSpriteModel(CastleExcellentSession* session) : TileMapModel(session), session_(session) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
    const int total = count();
    QVector<Frame> frames;
    for (int f = 0; f < kPlayerFrames; ++f) frames << Frame{f * 2, 2, true, true, {}};
    for (const auto& frame : kObjectFrames) {
        if (frame[0] + frame[1] <= total) frames << Frame{frame[0], frame[1], false, true, {}};
    }
    // Descriptors no frame covers: one sprite each, so every byte is shown.
    QVector<bool> covered(total, false);
    for (const Frame& frame : frames) {
        for (int d = frame.first; d < frame.first + frame.count; ++d) covered[d] = true;
    }
    for (int d = 0; d < total; ++d) {
        if (!covered.at(d)) frames << Frame{d, 1, false, false, {}};
    }
    std::sort(frames.begin(), frames.end(), [](const Frame& a, const Frame& b) { return a.first < b.first; });
    int column = 0;
    int row = 0;
    for (Frame& frame : frames) {
        if (column + frame.count > kSheetColumns) {
            column = 0;
            ++row;
        }
        frame.at = QPoint(column, row);
        for (int i = 0; i < frame.count; ++i) positions_.insert(frame.first + i, QPoint(column + i, row));
        column += frame.count + 1;
    }
    frames_ = frames;
    rows_ = row + 1;
}

int CastleExcellentSpriteModel::count() const {
    return session_->bundle().resource(session_->art().descriptors).value(QStringLiteral("descriptors")).toArray().size();
}

int CastleExcellentSpriteModel::tileOf(int descriptor) const {
    const auto found = pending_.constFind(descriptor);
    if (found != pending_.constEnd()) return *found;
    return session_->bundle()
        .resource(session_->art().descriptors)
        .value(QStringLiteral("descriptors"))
        .toArray()
        .at(descriptor)
        .toObject()
        .value(QStringLiteral("tile"))
        .toInt();
}

int CastleExcellentSpriteModel::attributesOf(int descriptor) const {
    return session_->bundle()
        .resource(session_->art().descriptors)
        .value(QStringLiteral("descriptors"))
        .toArray()
        .at(descriptor)
        .toObject()
        .value(QStringLiteral("attributes"))
        .toInt();
}

int CastleExcellentSpriteModel::descriptorAt(QPoint cell) const {
    for (auto it = positions_.constBegin(); it != positions_.constEnd(); ++it) {
        if (it.value().x() == cell.x() && it.value().y() == cell.y() / 2) return it.key();
    }
    return -1;
}

QSize CastleExcellentSpriteModel::gridSize() const {
    return {kSheetColumns, rows_ * 2};
}

QString CastleExcellentSpriteModel::frameLabel(int frame) const {
    const Frame& f = frames_.at(frame);
    if (f.player) return tr("Player %1").arg(f.first / 2 + 1);
    if (!f.referenced) return tr("Unused %1").arg(f.first);
    QStringList types;
    for (const CastleExcellentObjectPreview& preview : session_->levels().objectPreviews()) {
        if (preview.descriptorOffset == f.first * 2) types << hex(preview.runtimeType);
    }
    return types.isEmpty() ? tr("Frame %1").arg(f.first) : tr("Object %1").arg(types.join(QLatin1Char(' ')));
}

// ---------------------------------------------------------------- view

QVector<sdk::Tool> CastleExcellentSpriteModel::tools() const {
    return {{QStringLiteral("select"), tr("Select a frame"), QStringLiteral("select")},
            {QStringLiteral("paint"), tr("Paint the sheet's pattern pair into sprites (right button takes one)"),
             QStringLiteral("paint")},
            {QStringLiteral("pick"), tr("Take a sprite's pattern pair as the brush"), QStringLiteral("pick")}};
}

QVector<sdk::ViewOption> CastleExcellentSpriteModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Sheet colours");
    palette.value = tr("SPR %1").arg(libraryPalette_);
    for (int p = 0; p < 4; ++p) palette.choices << tr("SPR %1").arg(p);
    return {palette};
}

int CastleExcellentSpriteModel::paletteNumber(const QString& label) {
    // The whole label: a translation may place the number anywhere.
    for (int p = 0; p < 4; ++p) {
        if (label == tr("SPR %1").arg(p)) return p;
    }
    return 0;
}

void CastleExcellentSpriteModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        libraryPalette_ = paletteNumber(value);
        emit contentChanged();
        emit selectionChanged();
    }
}

QImage CastleExcellentSpriteModel::render() const {
    const QSize grid = gridSize();
    QImage image(grid.width() * 8, grid.height() * 8, QImage::Format_ARGB32);
    image.fill(QColor(0, 0, 0, 0));
    QPainter painter(&image);
    const CastleExcellentPainter& art = session_->painter();
    for (const Frame& frame : frames_) {
        painter.fillRect(QRect(frame.at.x() * 8, frame.at.y() * 16, frame.count * 8, 16), art.backdrop());
        for (int i = 0; i < frame.count; ++i) {
            const int d = frame.first + i;
            painter.drawImage(QPoint((frame.at.x() + i) * 8, frame.at.y() * 16),
                              art.sprite(CastleExcellentSprite{tileOf(d), attributesOf(d), 0, 0}));
        }
    }
    return image;
}

// ---------------------------------------------------------------- cells

QVector<sdk::CellBrush> CastleExcellentSpriteModel::brushes() const {
    return {{tile_, tr("Pattern pair %1").arg(hex(tile_)), QColor()}};
}

void CastleExcellentSpriteModel::setCurrentBrush(int value) {
    tile_ = std::clamp(value, 0, 255);
    emit toolsChanged();
    emit selectionChanged();
}

QString CastleExcellentSpriteModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void CastleExcellentSpriteModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    const int tile = std::clamp(id.mid(5).toInt(), 0, 255);
    if (tile != tile_) {
        tile_ = tile;
        emit toolsChanged();
        emit selectionChanged();
    }
}

int CastleExcellentSpriteModel::cellValue(QPoint cell) const {
    const int d = descriptorAt(cell);
    return d < 0 ? -1 : tileOf(d);
}

bool CastleExcellentSpriteModel::isCellEditable(QPoint cell) const {
    return descriptorAt(cell) >= 0;
}

bool CastleExcellentSpriteModel::setDescriptors(const QHash<int, QPair<int, int>>& values, const QString& text, QString* error) {
    const QString id = session_->art().descriptors;
    return session_->editBundle(text,
                                [id, values](nes::Bundle& b) {
                                    QJsonObject resource = b.resource(id);
                                    QJsonArray list = resource.value(QStringLiteral("descriptors")).toArray();
                                    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                                        QJsonObject d = list.at(it.key()).toObject();
                                        d.insert(QStringLiteral("tile"), it.value().first);
                                        d.insert(QStringLiteral("attributes"), it.value().second);
                                        list.replace(it.key(), d);
                                    }
                                    resource.insert(QStringLiteral("descriptors"), list);
                                    b.setResource(id, resource);
                                },
                                error);
}

bool CastleExcellentSpriteModel::placeObject(const QString& libraryItem, QPoint cell, QString* error) {
    const int d = descriptorAt(cell);
    if (!libraryItem.startsWith(QLatin1String("tile:")) || d < 0) {
        if (error) *error = tr("Drop a pattern pair onto a sprite.");
        return false;
    }
    const int tile = std::clamp(libraryItem.mid(5).toInt(), 0, 255);
    setCurrentLibraryItem(libraryItem);
    return setDescriptors({{d, qMakePair(tile, attributesOf(d))}}, tr("Set the patterns of sprite %1").arg(d),
                          error);
}

void CastleExcellentSpriteModel::beginStroke() {
    stroking_ = true;
    pending_.clear();
}

bool CastleExcellentSpriteModel::paintCell(QPoint cell, int value) {
    const int d = descriptorAt(cell);
    if (!stroking_ || d < 0 || value < 0 || value > 255 || tileOf(d) == value) return false;
    pending_.insert(d, value);
    emit contentChanged();
    return true;
}

void CastleExcellentSpriteModel::endStroke() {
    stroking_ = false;
    if (pending_.isEmpty()) return;
    QHash<int, QPair<int, int>> values;
    for (auto it = pending_.constBegin(); it != pending_.constEnd(); ++it) {
        values.insert(it.key(), qMakePair(it.value(), attributesOf(it.key())));
    }
    // Cleared first: the edit redraws from the document.
    pending_.clear();
    QString error;
    setDescriptors(values, tr("Paint sprites"), &error);
}

// ---------------------------------------------------------------- objects

QVector<sdk::MapObject> CastleExcellentSpriteModel::objects() const {
    QVector<sdk::MapObject> result;
    for (int f = 0; f < frames_.size(); ++f) {
        sdk::MapObject o;
        o.id = QStringLiteral("frame:%1").arg(f);
        o.label = frameLabel(f);
        o.cells = QRect(frames_.at(f).at.x(), frames_.at(f).at.y() * 2, frames_.at(f).count, 2);
        o.movable = false;
        o.outlined = false;
        result << o;
    }
    return result;
}

QString CastleExcellentSpriteModel::selectedObject() const {
    return selected_ >= 0 ? QStringLiteral("frame:%1").arg(selected_) : QString();
}

void CastleExcellentSpriteModel::selectObject(const QString& id) {
    const int frame = id.startsWith(QLatin1String("frame:")) ? id.mid(6).toInt() : -1;
    if (frame != selected_ && frame < frames_.size()) {
        selected_ = frame;
        if (frame >= 0) libraryPalette_ = attributesOf(frames_.at(frame).first) & 3;
        emit selectionChanged();
        emit contentChanged();
    }
}

// ---------------------------------------------------------------- inspector

bool CastleExcellentSpriteModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0 || key.size() < 2) return TileMapModel::setField(key, value, error);
    const Frame& frame = frames_.at(selected_);
    const int i = key.mid(1).toInt();
    if (i < 0 || i >= frame.count) return TileMapModel::setField(key, value, error);
    const int d = frame.first + i;
    int tile = tileOf(d);
    int attributes = attributesOf(d);
    switch (key.at(0).toLatin1()) {
    case 't': tile = std::clamp(value.toInt(), 0, 255); break;
    case 'p':
        attributes = (attributes & ~3) | paletteNumber(value.toString());
        break;
    case 'f': {
        const int flip = std::max(0, flipNames().indexOf(value.toString()));
        attributes = (attributes & ~0xC0) | ((flip & 1) ? 0x40 : 0) | ((flip & 2) ? 0x80 : 0);
        break;
    }
    default: return TileMapModel::setField(key, value, error);
    }
    return setDescriptors({{d, qMakePair(tile, attributes)}}, tr("Edit sprite %1").arg(d), error);
}

sdk::Library CastleExcellentSpriteModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.cell = QSize(1, 2);
    result.hint = tr("click: brush · drag onto a sprite");
    const CastleExcellentPainter& art = session_->painter();
    // 8x16 pairs: even numbers from the first pattern table, odd from the second.
    for (int table = 0; table < 2; ++table) {
        for (int pair = 0; pair < 128; ++pair) {
            const int tile = pair * 2 + table;
            QImage icon(8, 16, QImage::Format_ARGB32);
            icon.fill(art.backdrop());
            QPainter p(&icon);
            p.drawImage(0, 0, art.sprite(CastleExcellentSprite{tile, libraryPalette_, 0, 0}));
            p.end();
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = hex(tile);
            item.icon = icon;
            result.items << item;
        }
    }
    return result;
}

sdk::Inspection CastleExcellentSpriteModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle-excellent.metasprite-descriptors.v1");
    if (selected_ < 0 || selected_ >= frames_.size()) {
        inspection.kicker = tr("Sprites");
        inspection.title = title();
        sdk::Field sprites;
        sprites.key = QStringLiteral("sprites");
        sprites.label = tr("Sprites");
        sprites.value = QStringLiteral("%1 · 8x16").arg(count());
        inspection.fields = {sprites};
        inspection.note = tr("The player's and the objects' frames, each a row of 8x16 sprites: a pattern "
                             "pair (even from the first pattern table, odd from the second) and its "
                             "attributes. Paint a pattern pair from the sheet into a sprite, or select a "
                             "frame to set palettes and flips.");
        return inspection;
    }
    const Frame& frame = frames_.at(selected_);
    inspection.kicker = frame.player ? tr("Player frame") : tr("Object frame");
    inspection.title = frameLabel(selected_);
    for (int i = 0; i < frame.count; ++i) {
        const int d = frame.first + i;
        const int attributes = attributesOf(d);
        const QString which = frame.count > 1 ? tr("Sprite %1 ").arg(i + 1) : QString();
        sdk::Field tile;
        tile.key = QStringLiteral("t%1").arg(i);
        tile.label = which + QStringLiteral("patterns");
        tile.type = sdk::Field::Type::Integer;
        tile.value = tileOf(d);
        tile.minimum = 0;
        tile.maximum = 255;
        tile.hint = hex(tileOf(d));
        tile.monospace = true;
        sdk::Field palette;
        palette.key = QStringLiteral("p%1").arg(i);
        palette.label = which + QStringLiteral("palette");
        palette.type = sdk::Field::Type::Choice;
        for (int p = 0; p < 4; ++p) {
            palette.choices << tr("SPR %1").arg(p);
            palette.choiceIcons << session_->painter().sprite(CastleExcellentSprite{tileOf(d), (attributes & ~3) | p, 0, 0});
        }
        palette.value = tr("SPR %1").arg(attributes & 3);
        sdk::Field flip;
        flip.key = QStringLiteral("f%1").arg(i);
        flip.label = which + QStringLiteral("flip");
        flip.type = sdk::Field::Type::Choice;
        flip.choices = flipNames();
        flip.value = flipNames().at(((attributes & 0x40) ? 1 : 0) | ((attributes & 0x80) ? 2 : 0));
        inspection.fields << tile << palette << flip;
    }
    inspection.note = frame.player
                          ? tr("A frame of the player, two sprites side by side. Every animation that "
                               "shows it changes with it.")
                      : frame.referenced
                          ? tr("A frame the objects' code draws, its sprites 8 pixels apart. Every object "
                               "and animation step that shows it changes with it.")
                          : tr("A sprite no frame of the game's code draws.");
    inspection.engine = {{tr("Descriptors"),
                          QStringLiteral("%1-%2").arg(frame.first).arg(frame.first + frame.count - 1)},
                         {QStringLiteral("CPU"), QStringLiteral("$%1").arg(0xB4B6 + frame.first * 2, 4, 16, QLatin1Char('0')).toUpper()}};
    return inspection;
}

sdk::Budget CastleExcellentSpriteModel::budget() const {
    sdk::Budget budget;
    budget.used = count();
    budget.capacity = count();
    budget.text = tr("%1 sprites in %2 frames · fixed").arg(count()).arg(frames_.size());
    return budget;
}

QString CastleExcellentSpriteModel::statusText() const {
    return selected_ < 0 ? tr("Sprites · %1").arg(count()) : tr("Sprites · %1").arg(frameLabel(selected_));
}

}  // namespace rl::castle_excellent
