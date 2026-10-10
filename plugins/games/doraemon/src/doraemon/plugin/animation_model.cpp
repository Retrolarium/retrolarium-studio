#include "doraemon/plugin/animation_model.hpp"

#include "doraemon/plugin/doraemon_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::doraemon {

namespace {

// The number in a label made from `pattern` (a translated "BG %1"),
// matched whole: a translation may place the number anywhere. -1 for none.
int labelNumber(const QString& label, const QString& pattern, int count) {
    for (int n = 0; n < count; ++n) {
        if (label == pattern.arg(n)) return n;
    }
    return -1;
}

QVector<int> run(int first, int count) {
    QVector<int> frames;
    for (int i = 0; i < count; ++i) frames << first + i;
    return frames;
}

DoraemonAnimation make(World world, const char* group, const QString& label, QVector<int> frames, int ticks = 8,
                       int palette = 0) {
    return DoraemonAnimation{world, QString::fromLatin1(group), label, std::move(frames), ticks, palette};
}

const char* const kFlips[4] = {"None", "Horizontal", "Vertical", "Both"};

}  // namespace

namespace {

int world3Table(const DoraemonSession* session, const char* name, int type) {
    if (!session->bundle().contains(QStringLiteral("render-tables"))) return 0;
    return session->resource(QStringLiteral("render-tables"))
        .value(QStringLiteral("tables")).toObject().value(QLatin1String(name)).toArray().at(type & 0x1F).toInt();
}

}  // namespace

bool hasWorld3Tables(const DoraemonSession* session) {
    return session->bundle().contains(QStringLiteral("render-tables"));
}

int world3TypeMetasprite(const DoraemonSession* session, int type) {
    return world3Table(session, "world3_entity_metasprites", type);
}

int world3TypeFlags(const DoraemonSession* session, int type) {
    return world3Table(session, "world3_entity_render_flags", type);
}

QVector<SpritePiece> metaspritePieces(const DoraemonSession* session, World world, int index) {
    QVector<SpritePiece> result;
    const QJsonObject r = session->resource(metaspriteId(world));
    const QJsonArray entries = r.value(QStringLiteral("entries")).toArray();
    QJsonObject entry = entries.at(index).toObject();
    int flip = -1;
    if (entry.contains(QStringLiteral("alias"))) {
        flip = entry.value(QStringLiteral("flip")).toInt();
        entry = entries.at(entry.value(QStringLiteral("alias")).toInt()).toObject();
    }
    if (!entry.contains(QStringLiteral("record"))) return result;
    const QJsonObject rec = r.value(QStringLiteral("records")).toArray().at(entry.value(QStringLiteral("record")).toInt()).toObject();
    const int xExtent = rec.value(QStringLiteral("x_extent")).toInt();
    const int yExtent = rec.value(QStringLiteral("y_extent")).toInt();
    for (const QJsonValue& v : rec.value(QStringLiteral("pieces")).toArray()) {
        const QJsonArray piece = v.toArray();
        SpritePiece p;
        p.y = piece.at(0).toInt() & 0x7f;
        p.x = piece.at(1).toInt() & 0x7f;
        p.flipY = (piece.at(0).toInt() & 0x80) != 0;
        p.flipX = (piece.at(1).toInt() & 0x80) != 0;
        if (flip > 0 && (flip & 2)) {
            p.y = yExtent - p.y;
            p.flipY = !p.flipY;
        }
        if (flip > 0 && (flip & 1)) {
            p.x = xExtent - p.x;
            p.flipX = !p.flipX;
        }
        p.tile = piece.at(2).toInt();
        result << p;
    }
    return result;
}

QImage metaspriteImage(const DoraemonSession* session, World world, int index, const QVector<int>& colours,
                       QPoint* origin) {
    const QVector<SpritePiece> pieces = metaspritePieces(session, world, index);
    QRect box;
    for (const SpritePiece& p : pieces) box |= QRect(p.x, p.y, 8, 8);
    if (box.isEmpty()) box = QRect(0, 0, 8, 8);
    QImage image(box.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (const SpritePiece& p : pieces) {
        const QImage tile = session->painter().tile(world, p.tile, colours, true).mirrored(p.flipX, p.flipY);
        painter.drawImage(p.x - box.left(), p.y - box.top(), tile);
    }
    if (origin) *origin = box.topLeft();
    return image;
}

QImage world2Image(const DoraemonSession* session, int index, int preset) {
    const QJsonObject r = session->resource(metaspriteId(World::Two));
    const QJsonArray tiles = r.value(QStringLiteral("tiles")).toArray().at(index).toArray();
    const int descriptor = r.value(QStringLiteral("descriptors")).toArray().at(index).toInt();
    const QJsonArray attributes = r.value(QStringLiteral("attributes")).toArray();
    const QVector<int> colours = session->painter().colours(World::Two, preset, descriptor & 3);
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (int q = 0; q < 4; ++q) {
        const int tile = tiles.at(q).toInt();
        if (tile == 0) continue;
        const int a = attributes.at(((descriptor >> 2) & 0x0F) * 4 + q).toInt();
        painter.drawImage((q % 2) * 8, (q / 2) * 8,
                          session->painter().tile(World::Two, tile, colours, true).mirrored((a & 0x40) != 0, (a & 0x80) != 0));
    }
    return image;
}

QString metaspriteId(World world) {
    // World 2's enemy table was read 16 bytes early before the kit named it
    // world2-enemy-metasprites; projects with the old id are upgraded.
    if (world == World::Two) return QStringLiteral("world2-enemy-metasprites");
    return QStringLiteral("world%1-metasprites").arg(int(world));
}

const QVector<DoraemonAnimation>& doraemonAnimations() {
    // From the source reconstruction's runtime docs: World 1's player
    // selects direction * 4 + frame and steps every five updates; enemy and
    // item bases from the placement and descriptor tables; World 2's enemy
    // states with their metasprites; World 3's 32 entity types with their
    // bases and render-flag palettes.
    static const QVector<DoraemonAnimation> kAnimations = [] {
        QVector<DoraemonAnimation> list;
        const World one = World::One;
        list << make(one, "Doraemon", rlTr("doraemon", "Walk down"), run(0x00, 4), 5)
             << make(one, "Doraemon", rlTr("doraemon", "Walk up"), run(0x04, 4), 5)
             << make(one, "Doraemon", rlTr("doraemon", "Walk left"), run(0x08, 4), 5)
             << make(one, "Doraemon", rlTr("doraemon", "Walk right"), run(0x0C, 4), 5)
             << make(one, "Doraemon", rlTr("doraemon", "Hurt"), run(0x10, 2), 4);
        struct Enemy {
            int base;
            const char* name;
        };
        const Enemy enemies[] = {{0x3E, "W1Enemy3E"}, {0x4C, "W1Enemy4C"}, {0x54, "Nezumi"}, {0x56, "W1Enemy56"},
                                 {0x58, "W1Enemy58"},    {0x5C, "W1Enemy5C"}, {0x60, "W1Enemy60"}, {0x64, "W1Enemy64"},
                                 {0x6C, "W1Enemy6C"}, {0x70, "W1Enemy70"}};
        for (int i = 0; i < 10; ++i) {
            const int next = i + 1 < 10 ? enemies[i + 1].base : enemies[i].base + 4;
            list << make(one, "Enemies", QString::fromLatin1(enemies[i].name),
                         run(enemies[i].base, std::min(4, next - enemies[i].base)));
        }
        struct Item {
            int index;
            const char* name;
            int palette;
        };
        const Item items[] = {{0x29, "Manhole", 1},        {0x25, "Warp Door", 1}, {0x2E, "Stopwatch", 1},
                              {0x2D, "Energy Candy", 0},    {0x2F, "1UP", 1},           {0x2A, "Shock Gun", 1},
                              {0x2B, "Air Gun", 1},        {0x2C, "Power Fan", 1},     {0x30, "Pancake", 1},
                              {0x34, "Rapid-Fire Drink", 1}, {0x16, "Shrink Light", 1}, {0x14, "Programmer's face", 1},
                              {0x28, "Gold Bar", 1},       {0x17, "Diamond", 0},       {0x35, "Invulnerability", 3}};
        for (const Item& item : items) list << make(one, "Items", QString::fromLatin1(item.name), {item.index}, 8, item.palette);
        list << make(one, "Frames", rlTr("doraemon", "World 1 · all frames"), run(0, 115), 15);

        const World two = World::Two;
        struct Foe {
            const char* name;
            QVector<int> frames;
        };
        const QVector<Foe> foes{{"W2Enemy1", {0, 1}},     {"W2Enemy2", {2, 3}},           {"W2Enemy3", {4, 5, 6}},
                                {"W2Enemy4", {7, 8}},   {"W2Enemy5", {9, 10, 11}},    {"W2Enemy6", {12, 13}},
                                {"W2Enemy7", {14, 15}},    {"W2Creature7", {17, 18}},         {"Jura", {23, 24, 25}},
                                {"W2Enemy13", {30, 31}},      {"W2Enemy11", {32, 33, 34, 35}},   {"W2Enemy10", {36, 37}},
                                {"W2Enemy12", {38, 39}},       {"Big Robot Ship", {40, 41, 42, 44, 45, 46}},
                                {"Centaurus", {48, 49, 50, 52, 53, 54}}, {"Robot Ship", {56, 57}}};
        for (const Foe& foe : foes) list << make(two, "Enemies", QString::fromLatin1(foe.name), foe.frames);
        list << make(two, "Frames", rlTr("doraemon", "World 2 · all frames"), run(0, 58), 15);

        const World three = World::Three;
        list << make(three, "Doraemon", rlTr("doraemon", "Swim (one way)"), run(0x00, 3), 8)
             << make(three, "Doraemon", rlTr("doraemon", "Swim (other way)"), run(0x03, 3), 8);
        // Entity types $00-$1F: their frames and palette come from the
        // game's tables when one opens (resolvedAnimation).
        for (int t = 0; t < 32; ++t) {
            DoraemonAnimation creature = make(three, "Creatures", rlTr("doraemon", "Type %1").arg(nes::hex(t)), {}, 8);
            creature.world3Type = t;
            list << creature;
        }
        list << make(three, "Frames", rlTr("doraemon", "World 3 · all frames"), run(0, 188), 15);
        return list;
    }();
    return kAnimations;
}

DoraemonAnimation resolvedAnimation(const DoraemonSession* session, int index) {
    DoraemonAnimation a = doraemonAnimations().at(index);
    if (a.world3Type < 0) return a;
    // A type's frames run from its base for up to four, inside the table.
    const int count = session->resource(metaspriteId(World::Three)).value(QStringLiteral("entries")).toArray().size();
    const int base = world3TypeMetasprite(session, a.world3Type);
    for (int f = base; f < std::min(base + 4, count); ++f) a.frames << f;
    a.palette = world3TypeFlags(session, a.world3Type) & 3;
    return a;
}

DoraemonAnimationModel::DoraemonAnimationModel(DoraemonSession* session, int animation)
    : CompositionModel(session), session_(session), index_(animation),
      animation_(resolvedAnimation(session, animation)), palette_(animation_.palette) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

const DoraemonAnimation& DoraemonAnimationModel::animation() const {
    return animation_;
}

int DoraemonAnimationModel::index() const {
    return animation().frames.value(std::clamp(frame_, 0, int(animation().frames.size()) - 1));
}

QString DoraemonAnimationModel::title() const {
    const DoraemonAnimation& a = animation();
    if (a.group == QLatin1String("Frames")) return a.label;
    return QStringLiteral("%1 · %2").arg(a.group == QLatin1String("Doraemon") ? QStringLiteral("Doraemon") : worldLabel(a.world), a.label);
}

int DoraemonAnimationModel::recordOf(int index, int* flip) const {
    const QJsonArray entries = session_->resource(metaspriteId(animation().world)).value(QStringLiteral("entries")).toArray();
    QJsonObject entry = entries.at(index).toObject();
    *flip = -1;
    if (entry.contains(QStringLiteral("alias"))) {
        *flip = entry.value(QStringLiteral("flip")).toInt();
        entry = entries.at(entry.value(QStringLiteral("alias")).toInt()).toObject();
    }
    // The renderer resolves one alias level; a second one draws nothing.
    return entry.contains(QStringLiteral("record")) ? entry.value(QStringLiteral("record")).toInt() : -1;
}

QVector<QVector<int>> DoraemonAnimationModel::spritePalettes() const {
    QVector<QVector<int>> result;
    if (animation().world == World::Two) {
        // Chapter sprite sets are World 2 presets 6-8, sixteen colours each.
        const int preset = 6 + std::clamp(preset_, 0, 2);
        for (int row = 0; row < 4; ++row) result << session_->painter().colours(World::Two, preset, row);
        return result;
    }
    for (int row = 4; row < 8; ++row) result << session_->painter().colours(animation().world, preset_, row);
    return result;
}

QVector<DoraemonAnimationModel::Placed> DoraemonAnimationModel::placed() const {
    QVector<Placed> result;
    const QJsonObject r = session_->resource(metaspriteId(animation().world));
    const int i = index();
    if (animation().world == World::Two) {
        const QJsonArray tiles = r.value(QStringLiteral("tiles")).toArray().at(i).toArray();
        const int descriptor = r.value(QStringLiteral("descriptors")).toArray().at(i).toInt();
        const QJsonArray attributes = r.value(QStringLiteral("attributes")).toArray();
        for (int q = 0; q < 4; ++q) {
            const int a = attributes.at(((descriptor >> 2) & 0x0F) * 4 + q).toInt();
            Placed p;
            p.piece = q;
            p.x = (q % 2) * 8;
            p.y = (q / 2) * 8;
            p.tile = tiles.at(q).toInt();
            p.flipX = (a & 0x40) != 0;
            p.flipY = (a & 0x80) != 0;
            p.palette = descriptor & 3;
            p.hidden = p.tile == 0;
            result << p;
        }
        return result;
    }
    int flip = -1;
    const int record = recordOf(i, &flip);
    if (record < 0) return result;
    const QJsonObject rec = r.value(QStringLiteral("records")).toArray().at(record).toObject();
    const int xExtent = rec.value(QStringLiteral("x_extent")).toInt();
    const int yExtent = rec.value(QStringLiteral("y_extent")).toInt();
    const QJsonArray pieces = rec.value(QStringLiteral("pieces")).toArray();
    for (int k = 0; k < pieces.size(); ++k) {
        const QJsonArray piece = pieces.at(k).toArray();
        const int rawY = piece.at(0).toInt();
        const int rawX = piece.at(1).toInt();
        Placed p;
        p.piece = k;
        p.y = rawY & 0x7f;
        p.x = rawX & 0x7f;
        p.flipY = (rawY & 0x80) != 0;
        p.flipX = (rawX & 0x80) != 0;
        if (flip > 0 && (flip & 2)) {
            p.y = yExtent - p.y;
            p.flipY = !p.flipY;
        }
        if (flip > 0 && (flip & 1)) {
            p.x = xExtent - p.x;
            p.flipX = !p.flipX;
        }
        p.tile = piece.at(2).toInt();
        p.palette = palette_;
        // A sprite being dragged, where the drag has it.
        if (dragging_ && k == dragPiece_ && dragAt_.x() >= 0) {
            p.x = dragAt_.x();
            p.y = dragAt_.y();
        }
        result << p;
    }
    return result;
}

QImage DoraemonAnimationModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    const QVector<QVector<int>> palettes = spritePalettes();
    image.fill(session_->painter().color(palettes.value(0).value(0, 0x0f)));
    QPainter painter(&image);
    for (const Placed& p : placed()) {
        if (p.hidden) continue;
        QImage tile = session_->painter().tile(animation().world, p.tile, palettes.value(p.palette & 3), true);
        tile = tile.mirrored(p.flipX, p.flipY);
        painter.drawImage(origin() + QPoint(p.x, p.y), tile);
    }
    return image;
}

QRect DoraemonAnimationModel::focusRect() const {
    // Every frame of the animation, so playing it does not jump.
    QRect box;
    const int saved = frame_;
    auto* self = const_cast<DoraemonAnimationModel*>(this);
    for (int f = 0; f < animation().frames.size(); ++f) {
        self->frame_ = f;
        for (const Placed& p : placed()) box |= QRect(origin() + QPoint(p.x, p.y), QSize(8, 8));
    }
    self->frame_ = saved;
    if (box.isEmpty()) box = QRect(origin(), QSize(16, 16));
    return box.adjusted(-8, -8, 8, 8).intersected(QRect(QPoint(0, 0), canvasSize()));
}

QVector<sdk::CompositionPiece> DoraemonAnimationModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    int flip = -1;
    if (animation().world != World::Two) recordOf(index(), &flip);
    for (const Placed& p : placed()) {
        sdk::CompositionPiece piece;
        piece.id = QStringLiteral("p:%1").arg(p.piece);
        piece.label = tr("Sprite %1 · %2").arg(p.piece + 1).arg(nes::hex(p.tile));
        piece.pixels = QRect(origin() + QPoint(p.x, p.y), QSize(8, 8));
        // Mirrored aliases and World 2's fixed grid do not move.
        piece.movable = animation().world != World::Two && flip <= 0;
        result << piece;
    }
    return result;
}

QString DoraemonAnimationModel::selectedPiece() const {
    return selected_ >= 0 ? QStringLiteral("p:%1").arg(selected_) : QString();
}

void DoraemonAnimationModel::selectPiece(const QString& id) {
    const int piece = id.startsWith(QLatin1String("p:")) ? id.mid(2).toInt() : -1;
    if (piece != selected_) {
        selected_ = piece;
        emit selectionChanged();
    }
}

QVector<sdk::CompositionFrame> DoraemonAnimationModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QJsonArray entries = session_->resource(metaspriteId(animation().world)).value(QStringLiteral("entries")).toArray();
    for (int f = 0; f < animation().frames.size(); ++f) {
        const int i = animation().frames.at(f);
        sdk::CompositionFrame frame;
        frame.id = QString::number(f);
        frame.label = tr("Frame %1").arg(nes::hex(i));
        const QJsonObject entry = entries.at(i).toObject();
        frame.detail = entry.contains(QStringLiteral("alias"))
                           ? tr("mirrors %1").arg(nes::hex(entry.value(QStringLiteral("alias")).toInt()))
                           : tr("%n ticks", nullptr, animation().ticks);
        result << frame;
    }
    return result;
}

void DoraemonAnimationModel::setCurrentFrame(int frame) {
    if (frame != frame_ && frame >= 0 && frame < animation().frames.size()) {
        frame_ = frame;
        selected_ = -1;
        emit selectionChanged();
        emit contentChanged();
    }
}

int DoraemonAnimationModel::frameDurationMs(int) const {
    return animation().ticks * 1000 / 60;
}

QVector<sdk::ViewOption> DoraemonAnimationModel::viewOptions() const {
    QVector<sdk::ViewOption> result;
    sdk::ViewOption colours;
    colours.key = QStringLiteral("preset");
    if (animation().world == World::Two) {
        colours.label = tr("Chapter");
        for (int c = 0; c < 3; ++c) colours.choices << tr("Chapter %1").arg(c + 1);
        colours.value = tr("Chapter %1").arg(preset_ + 1);
        return {colours};
    }
    colours.label = tr("Colours");
    for (int p = 0; p < paletteIds(session_->bundle(), animation().world).size(); ++p) colours.choices << tr("Preset %1").arg(p);
    colours.value = tr("Preset %1").arg(preset_);
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Palette");
    for (int p = 0; p < 4; ++p) palette.choices << tr("SPR %1").arg(p);
    palette.value = tr("SPR %1").arg(palette_);
    return {colours, palette};
}

void DoraemonAnimationModel::setViewOption(const QString& key, const QString& value) {
    const bool chapters = animation().world == World::Two;
    if (key == QLatin1String("preset")) {
        const int n = labelNumber(value, chapters ? tr("Chapter %1") : tr("Preset %1"), 64);
        if (n < 0) return;
        preset_ = chapters ? std::clamp(n - 1, 0, 2) : n;
    } else if (key == QLatin1String("palette")) {
        const int n = labelNumber(value, tr("SPR %1"), 4);
        if (n < 0) return;
        palette_ = n;
    } else {
        return;
    }
    emit toolsChanged();
    emit contentChanged();
}

bool DoraemonAnimationModel::editPiece(int piece, int rawY, int rawX, int tile, const QString& text, QString* error) {
    int flip = -1;
    const int record = recordOf(index(), &flip);
    if (record < 0) return false;
    const QString id = metaspriteId(animation().world);
    return session_->editBundle(
        text,
        [id, record, piece, rawY, rawX, tile](nes::Bundle& b) {
            QJsonObject r = b.resource(id);
            QJsonArray records = r.value(QStringLiteral("records")).toArray();
            QJsonObject rec = records.at(record).toObject();
            QJsonArray pieces = rec.value(QStringLiteral("pieces")).toArray();
            pieces.replace(piece, QJsonArray{rawY & 0xff, rawX & 0xff, tile & 0xff});
            rec.insert(QStringLiteral("pieces"), pieces);
            records.replace(record, rec);
            r.insert(QStringLiteral("records"), records);
            b.setResource(id, r);
        },
        error);
}

bool DoraemonAnimationModel::editWorld2(int quarter, int tile, int attribute, int descriptor, const QString& text, QString* error) {
    const int i = index();
    const QString id = metaspriteId(World::Two);
    return session_->editBundle(
        text,
        [id, i, quarter, tile, attribute, descriptor](nes::Bundle& b) {
            QJsonObject r = b.resource(id);
            QJsonArray tiles = r.value(QStringLiteral("tiles")).toArray();
            QJsonArray quad = tiles.at(i).toArray();
            quad.replace(quarter, tile);
            tiles.replace(i, quad);
            QJsonArray descriptors = r.value(QStringLiteral("descriptors")).toArray();
            descriptors.replace(i, descriptor);
            QJsonArray attributes = r.value(QStringLiteral("attributes")).toArray();
            attributes.replace(((descriptor >> 2) & 0x0F) * 4 + quarter, attribute);
            r.insert(QStringLiteral("tiles"), tiles);
            r.insert(QStringLiteral("descriptors"), descriptors);
            r.insert(QStringLiteral("attributes"), attributes);
            b.setResource(id, r);
        },
        error);
}

void DoraemonAnimationModel::beginDrag(const QString& id) {
    selectPiece(id);
    dragPiece_ = selected_;
    dragAt_ = QPoint(-1, -1);
    for (const Placed& p : placed()) {
        if (p.piece == selected_) dragStart_ = QPoint(p.x, p.y);
    }
    dragging_ = true;
}

bool DoraemonAnimationModel::dragPiece(const QString& id, QPoint offset) {
    if (!dragging_ || animation().world == World::Two || id.mid(2).toInt() != dragPiece_) return false;
    int flip = -1;
    if (recordOf(index(), &flip) < 0 || flip > 0) return false;
    const QPoint at(std::clamp(dragStart_.x() + offset.x(), 0, 127), std::clamp(dragStart_.y() + offset.y(), 0, 127));
    if (at == dragAt_) return false;
    dragAt_ = at;
    emit contentChanged();
    return true;
}

void DoraemonAnimationModel::endDrag() {
    if (!dragging_) return;
    const QPoint at = dragAt_;
    const int piece = dragPiece_;
    dragging_ = false;
    dragAt_ = QPoint(-1, -1);
    if (at.x() < 0 || at == dragStart_) {
        emit contentChanged();
        return;
    }
    // One drag, one undo step.
    for (const Placed& p : placed()) {
        if (p.piece != piece) continue;
        QString error;
        editPiece(piece, at.y() | (p.flipY ? 0x80 : 0), at.x() | (p.flipX ? 0x80 : 0), p.tile,
                  tr("Move a sprite of %1").arg(title()), &error);
    }
    emit contentChanged();
}

bool DoraemonAnimationModel::dropItem(const QString& libraryItem, const QString& piece, QString* error) {
    const int k = piece.startsWith(QLatin1String("p:")) ? piece.mid(2).toInt() : -1;
    if (!libraryItem.startsWith(QLatin1String("tile:")) || k < 0) {
        if (error) *error = tr("Drop a sprite pattern onto a sprite.");
        return false;
    }
    selectPiece(piece);
    return setField(QStringLiteral("tile"), libraryItem.mid(5).toInt(), error);
}

bool DoraemonAnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0) return CompositionModel::setField(key, value, error);
    Placed p;
    bool found = false;
    for (const Placed& candidate : placed()) {
        if (candidate.piece == selected_) {
            p = candidate;
            found = true;
        }
    }
    if (!found) return false;
    const QString text = tr("Edit a sprite of %1").arg(title());
    if (animation().world == World::Two) {
        const QJsonObject r = session_->resource(metaspriteId(World::Two));
        int descriptor = r.value(QStringLiteral("descriptors")).toArray().at(index()).toInt();
        int attribute = r.value(QStringLiteral("attributes")).toArray().at(((descriptor >> 2) & 0x0F) * 4 + p.piece).toInt();
        int tile = p.tile;
        if (key == QLatin1String("tile")) tile = std::clamp(value.toInt(), 0, 255);
        else if (key == QLatin1String("palette")) descriptor = (descriptor & ~3) | std::max(0, labelNumber(value.toString(), tr("SPR %1"), 4));
        else if (key == QLatin1String("flip")) {
            const int flip = std::max(0, QStringList{kFlips[0], kFlips[1], kFlips[2], kFlips[3]}.indexOf(value.toString()));
            attribute = (attribute & ~0xc0) | ((flip & 1) ? 0x40 : 0) | ((flip & 2) ? 0x80 : 0);
        } else if (key == QLatin1String("priority")) {
            attribute = (attribute & ~0x20) | (value.toString() == QLatin1String("Behind the background") ? 0x20 : 0);
        } else {
            return CompositionModel::setField(key, value, error);
        }
        return editWorld2(p.piece, tile, attribute, descriptor, text, error);
    }
    int flipMode = -1;
    if (recordOf(index(), &flipMode) < 0) return false;
    if (flipMode > 0) {
        if (error) *error = tr("This frame mirrors another; edit that frame.");
        return false;
    }
    int x = p.x;
    int y = p.y;
    int tile = p.tile;
    bool flipX = p.flipX;
    bool flipY = p.flipY;
    if (key == QLatin1String("tile")) tile = std::clamp(value.toInt(), 0, 255);
    else if (key == QLatin1String("x")) x = std::clamp(value.toInt(), 0, 127);
    else if (key == QLatin1String("y")) y = std::clamp(value.toInt(), 0, 127);
    else if (key == QLatin1String("flip")) {
        const int flip = std::max(0, QStringList{kFlips[0], kFlips[1], kFlips[2], kFlips[3]}.indexOf(value.toString()));
        flipX = flip & 1;
        flipY = flip & 2;
    } else {
        return CompositionModel::setField(key, value, error);
    }
    return editPiece(p.piece, y | (flipY ? 0x80 : 0), x | (flipX ? 0x80 : 0), tile, text, error);
}

sdk::Library DoraemonAnimationModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = tr("drag a pattern onto a sprite");
    const QVector<QVector<int>> palettes = spritePalettes();
    int palette = animation().world == World::Two ? 0 : palette_;
    for (const Placed& p : placed()) {
        if (p.piece == selected_) palette = p.palette;
    }
    const QColor backdrop = session_->painter().color(palettes.value(0).value(0, 0x0f));
    for (int tile = 0; tile < 256; ++tile) {
        QImage icon(8, 8, QImage::Format_ARGB32);
        icon.fill(backdrop);
        QPainter painter(&icon);
        painter.drawImage(0, 0, session_->painter().tile(animation().world, tile, palettes.value(palette & 3), true));
        painter.end();
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = nes::hex(tile);
        item.icon = icon;
        result.items << item;
    }
    return result;
}

sdk::Inspection DoraemonAnimationModel::inspect() const {
    sdk::Inspection inspection;
    const bool two = animation().world == World::Two;
    inspection.engineTag = two ? QStringLiteral("doraemon.world2-metasprites.v1") : QStringLiteral("doraemon.metasprites.v1");
    int flip = -1;
    const int record = two ? index() : recordOf(index(), &flip);
    if (selected_ < 0) {
        inspection.kicker = tr("Animation");
        inspection.title = title();
        sdk::Field frame;
        frame.key = QStringLiteral("frame");
        frame.label = tr("Shown");
        frame.value = two ? tr("metasprite %1").arg(index())
                          : flip > 0 ? tr("index %1 · record %2 mirrored %3").arg(nes::hex(index())).arg(record).arg(QString::fromLatin1(kFlips[flip]).toLower())
                                     : tr("index %1 · record %2").arg(nes::hex(index())).arg(record);
        inspection.fields = {frame};
        inspection.note = two ? tr("A World 2 metasprite is four sprites in a 2x2 square: four patterns, a "
                                   "palette and per-sprite attributes shared by records with the same base. "
                                   "Tile 0 leaves a sprite out. Chapter picks the chapter's sprite colours.")
                              : tr("Frames are records of 8x8 sprites at offsets from the actor's position; "
                                   "select a sprite to change its pattern, place or flips, drag it, or drop a "
                                   "pattern on it. Records are shared by every index that draws them; a "
                                   "mirrored frame is edited through its source. The catalog keeps its size. "
                                   "The game gives each actor its palette; Colours and Palette choose the one "
                                   "shown.");
        return inspection;
    }
    Placed p;
    for (const Placed& candidate : placed()) {
        if (candidate.piece == selected_) p = candidate;
    }
    inspection.kicker = tr("Sprite");
    inspection.title = tr("%1 · sprite %2").arg(frames().value(frame_).label).arg(selected_ + 1);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Pattern");
    tile.type = sdk::Field::Type::Integer;
    tile.value = p.tile;
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = nes::hex(p.tile);
    tile.monospace = true;
    sdk::Field flips;
    flips.key = QStringLiteral("flip");
    flips.label = tr("Flip");
    flips.type = sdk::Field::Type::Choice;
    for (const char* f : kFlips) flips.choices << QString::fromLatin1(f);
    flips.value = flips.choices.at((p.flipX ? 1 : 0) | (p.flipY ? 2 : 0));
    if (two) {
        sdk::Field palette;
        palette.key = QStringLiteral("palette");
        palette.label = tr("Palette");
        palette.type = sdk::Field::Type::Choice;
        for (int k = 0; k < 4; ++k) palette.choices << tr("SPR %1").arg(k);
        palette.value = tr("SPR %1").arg(p.palette);
        palette.hint = tr("the whole metasprite");
        const QJsonObject r = session_->resource(metaspriteId(World::Two));
        const int descriptor = r.value(QStringLiteral("descriptors")).toArray().at(index()).toInt();
        const int attribute = r.value(QStringLiteral("attributes")).toArray().at(((descriptor >> 2) & 0x0F) * 4 + p.piece).toInt();
        sdk::Field priority;
        priority.key = QStringLiteral("priority");
        priority.label = tr("Priority");
        priority.type = sdk::Field::Type::Choice;
        priority.choices = QStringList{tr("In front"), QStringLiteral("Behind the background")};
        priority.value = priority.choices.at((attribute & 0x20) ? 1 : 0);
        inspection.fields = {tile, palette, flips, priority};
        inspection.note = tr("The flip and priority belong to attribute group %1, shared by every metasprite "
                             "with that base.").arg((descriptor >> 2) & 0x0F);
        return inspection;
    }
    sdk::Field x;
    x.key = QStringLiteral("x");
    x.label = QStringLiteral("X");
    x.type = sdk::Field::Type::Integer;
    x.value = p.x;
    x.maximum = 127;
    sdk::Field y = x;
    y.key = QStringLiteral("y");
    y.label = QStringLiteral("Y");
    y.value = p.y;
    if (flip > 0) {
        tile.type = sdk::Field::Type::ReadOnly;
        x.type = sdk::Field::Type::ReadOnly;
        y.type = sdk::Field::Type::ReadOnly;
        flips.type = sdk::Field::Type::ReadOnly;
        inspection.note = tr("This frame mirrors index %1; edit that frame.")
                              .arg(nes::hex(session_->resource(metaspriteId(animation().world)).value(QStringLiteral("entries")).toArray()
                                                .at(index()).toObject().value(QStringLiteral("alias")).toInt()));
    } else {
        inspection.note = tr("Offsets are pixels from the actor's position (0-127).");
    }
    inspection.fields = {tile, x, y, flips};
    return inspection;
}

QString DoraemonAnimationModel::statusText() const {
    return tr("Animations · %1 · %2").arg(title(), frames().value(frame_).label);
}

}  // namespace rl::doraemon
