#include "castle_excellent/plugin/animation_model.hpp"

#include "castle_excellent/plugin/castle_excellent_session.hpp"
#include "nes/editors.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::castle_excellent {

namespace {

QVector<int> offsetsFor(int sprites) {
    QVector<int> offsets;
    for (int i = 0; i < sprites; ++i) offsets << i * 8;
    return offsets;
}

CastleExcellentAnimationFrame frame(int first, int sprites, int ticks = 8) {
    CastleExcellentAnimationFrame f;
    f.first = first;
    f.offsets = offsetsFor(sprites);
    f.ticks = ticks;
    return f;
}

CastleExcellentAnimation player(const char* id, const char* label, std::initializer_list<int> firsts, int ticks = 8) {
    CastleExcellentAnimation a;
    a.id = QStringLiteral("player:") + QString::fromLatin1(id);
    a.label = QString::fromLatin1(label);
    a.group = QStringLiteral("Player");
    for (int first : firsts) a.frames << frame(first, 2, ticks);
    return a;
}

QString typeName(int type) {
    switch (type) {
    case 0x81: return rlTr("castle_excellent", "Fairy");
    case 0x84: return rlTr("castle_excellent", "Fighter");
    case 0x85: return rlTr("castle_excellent", "Bishop");
    case 0x87: return rlTr("castle_excellent", "Knight");
    case 0x88: return rlTr("castle_excellent", "Witch");
    case 0x89: return rlTr("castle_excellent", "Flame");
    case 0x8e: return rlTr("castle_excellent", "Lift control block");
    case 0xa0: return rlTr("castle_excellent", "Witch summon");
    case 0xa1: return rlTr("castle_excellent", "Small lift");
    case 0xa2: return rlTr("castle_excellent", "Large lift");
    case 0xa3: return rlTr("castle_excellent", "Flying block");
    case 0xa4: return rlTr("castle_excellent", "Linked lift (left)");
    case 0xa5: return rlTr("castle_excellent", "Linked lift (right)");
    case 0xa6: return rlTr("castle_excellent", "Knight arrow");
    case 0xa8: return rlTr("castle_excellent", "Vanishing object");
    case 0xa9: return rlTr("castle_excellent", "Collected item");
    default: return {};
    }
}

CastleExcellentAnimation object(int type, std::initializer_list<int> firsts, int sprites = 2) {
    CastleExcellentAnimation a;
    a.id = QStringLiteral("object:%1").arg(type, 2, 16, QLatin1Char('0'));
    const QString name = typeName(type);
    a.label = name.isEmpty() ? rlTr("castle_excellent", "Object %1").arg(nes::hex(type))
                             : QStringLiteral("%1 · %2").arg(name, nes::hex(type));
    a.group = type == 0x81 || (type >= 0x84 && type <= 0x89) || type == 0xa0 || type == 0xa6 ? rlTr("castle_excellent", "Enemies")
                                                                             : rlTr("castle_excellent", "Objects");
    for (int first : firsts) a.frames << frame(first, sprites);
    return a;
}

}  // namespace

const QVector<CastleExcellentAnimation>& castleExcellentAnimations() {
    // The player's frames come from the source reconstruction's model of its
    // frame selection (scripts/authoring/frame_sprites.py); an object type's
    // from the descriptors its sprite selector references. Both revisions
    // share them, and the code that picks them is not edited.
    static const QVector<CastleExcellentAnimation> kAnimations = [] {
        QVector<CastleExcellentAnimation> list;
        list << player("walk-right", "Walk right", {0, 2, 4, 2});
        list << player("walk-left", "Walk left", {10, 12, 14, 12});
        list << player("stand", "Standing", {6});
        list << player("jump", "Jump", {16});
        list << player("jump-right", "Jump right", {8});
        list << player("jump-left", "Jump left", {18});
        CastleExcellentAnimation right = player("attack-right", "Attack right", {20});
        right.frames[0].weapon = true;
        CastleExcellentAnimation left = player("attack-left", "Attack left", {22});
        left.frames[0].weapon = true;
        left.frames[0].flipped = true;
        list << right << left;
        list << player("transition", "Room transition", {24, 26, 28, 30}, 4);
        list << object(0x81, {111, 113, 115});
        list << object(0x84, {32, 34, 36, 38, 40, 42, 44, 46});
        list << object(0x85, {48, 50, 52, 54, 56, 58});
        CastleExcellentAnimation odd = object(0x86, {61, 63, 65, 67, 69});
        odd.frames.prepend(frame(60, 1));
        list << odd;
        list << object(0x87, {71, 73, 75, 77, 79, 81});
        list << object(0x88, {83, 85, 87, 91, 93, 95});
        list << object(0x89, {99, 101, 103, 105, 107, 109});
        list << object(0xa0, {163, 165, 167});
        list << object(0xa6, {187, 188}, 1);
        list << object(0x82, {123}) << object(0x83, {131});
        for (int type = 0x8a; type <= 0x8e; ++type) list << object(type, {133 + (type - 0x8a) * 2});
        list << object(0x8f, {143, 145});
        for (int type = 0x90; type <= 0x95; ++type) list << object(type, {125 + type - 0x90}, 1);
        for (int type = 0x96; type <= 0x9c; ++type) list << object(type, {147 + (type - 0x96) * 2});
        list << object(0x9d, {163}) << object(0x9e, {163}) << object(0x9f, {169, 171});
        list << object(0xa1, {177});
        list << object(0xa2, {173}, 4);
        list << object(0xa3, {181}) << object(0xa4, {183}) << object(0xa5, {185});
        list << object(0xa8, {189, 191, 193}) << object(0xa9, {195, 197, 199, 201});
        return list;
    }();
    return kAnimations;
}

CastleExcellentAnimationModel::CastleExcellentAnimationModel(CastleExcellentSession* session, int animation)
    : CompositionModel(session), session_(session), index_(animation) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

const CastleExcellentAnimation& CastleExcellentAnimationModel::animation() const {
    return castleExcellentAnimations().at(index_);
}

QString CastleExcellentAnimationModel::title() const {
    return animation().label;
}

int CastleExcellentAnimationModel::tileOf(int descriptor) const {
    return session_->bundle().resource(session_->art().descriptors).value(QStringLiteral("descriptors")).toArray()
        .at(descriptor).toObject().value(QStringLiteral("tile")).toInt();
}

int CastleExcellentAnimationModel::attributesOf(int descriptor) const {
    return session_->bundle().resource(session_->art().descriptors).value(QStringLiteral("descriptors")).toArray()
        .at(descriptor).toObject().value(QStringLiteral("attributes")).toInt();
}

QImage CastleExcellentAnimationModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(session_->painter().backdrop());
    QPainter painter(&image);
    const CastleExcellentAnimationFrame& f = animation().frames.at(std::clamp(frame_, 0, int(animation().frames.size()) - 1));
    const QPoint at = origin();
    for (int i = 0; i < f.offsets.size(); ++i) {
        const int d = f.first + i;
        painter.drawImage(at + QPoint(f.offsets.at(i), 0), session_->painter().sprite(CastleExcellentSprite{tileOf(d), attributesOf(d), 0, 0}));
    }
    if (f.weapon) {
        // FramePlayerActionSprite: tile $CA beside the player, mirrored on the left.
        const CastleExcellentSprite weapon{0xca, f.flipped ? 0x43 : 0x03, 0, 0};
        painter.drawImage(at + QPoint(f.flipped ? -7 : 15, 0), session_->painter().sprite(weapon));
    }
    return image;
}

QVector<sdk::CompositionPiece> CastleExcellentAnimationModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const CastleExcellentAnimationFrame& f = animation().frames.at(std::clamp(frame_, 0, int(animation().frames.size()) - 1));
    for (int i = 0; i < f.offsets.size(); ++i) {
        sdk::CompositionPiece piece;
        piece.id = QStringLiteral("d:%1").arg(f.first + i);
        piece.label = tr("Sprite %1 · %2").arg(f.first + i).arg(nes::hex(tileOf(f.first + i)));
        piece.pixels = QRect(origin() + QPoint(f.offsets.at(i), 0), QSize(8, 16));
        piece.movable = false;
        result << piece;
    }
    return result;
}

QString CastleExcellentAnimationModel::selectedPiece() const {
    return selected_ >= 0 ? QStringLiteral("d:%1").arg(selected_) : QString();
}

void CastleExcellentAnimationModel::selectPiece(const QString& id) {
    const int descriptor = id.startsWith(QLatin1String("d:")) ? id.mid(2).toInt() : -1;
    if (descriptor != selected_) {
        selected_ = descriptor;
        emit selectionChanged();
    }
}

QVector<sdk::CompositionFrame> CastleExcellentAnimationModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QVector<CastleExcellentAnimationFrame>& list = animation().frames;
    for (int i = 0; i < list.size(); ++i) {
        sdk::CompositionFrame f;
        f.id = QString::number(i);
        f.label = tr("Frame %1").arg(i + 1);
        f.detail = tr("sprites %1-%2 · %3 ticks")
                       .arg(list.at(i).first)
                       .arg(list.at(i).first + list.at(i).offsets.size() - 1)
                       .arg(list.at(i).ticks);
        result << f;
    }
    return result;
}

void CastleExcellentAnimationModel::setCurrentFrame(int frame) {
    if (frame != frame_ && frame >= 0 && frame < animation().frames.size()) {
        frame_ = frame;
        selected_ = -1;
        emit selectionChanged();
        emit contentChanged();
    }
}

int CastleExcellentAnimationModel::frameDurationMs(int frame) const {
    return animation().frames.value(frame).ticks * 1000 / 60;
}

bool CastleExcellentAnimationModel::setDescriptor(int descriptor, int tile, int attributes, QString* error) {
    const QString id = session_->art().descriptors;
    return session_->editBundle(tr("Edit sprite %1").arg(descriptor),
                                [id, descriptor, tile, attributes](nes::Bundle& b) {
                                    QJsonObject resource = b.resource(id);
                                    QJsonArray list = resource.value(QStringLiteral("descriptors")).toArray();
                                    QJsonObject d = list.at(descriptor).toObject();
                                    d.insert(QStringLiteral("tile"), tile);
                                    d.insert(QStringLiteral("attributes"), attributes);
                                    list.replace(descriptor, d);
                                    resource.insert(QStringLiteral("descriptors"), list);
                                    b.setResource(id, resource);
                                },
                                error);
}

bool CastleExcellentAnimationModel::dropItem(const QString& libraryItem, const QString& piece, QString* error) {
    const int descriptor = piece.startsWith(QLatin1String("d:")) ? piece.mid(2).toInt() : -1;
    if (!libraryItem.startsWith(QLatin1String("tile:")) || descriptor < 0) {
        if (error) *error = tr("Drop a pattern pair onto a sprite.");
        return false;
    }
    selectPiece(piece);
    return setDescriptor(descriptor, std::clamp(libraryItem.mid(5).toInt(), 0, 255), attributesOf(descriptor), error);
}

bool CastleExcellentAnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0) return CompositionModel::setField(key, value, error);
    int tile = tileOf(selected_);
    int attributes = attributesOf(selected_);
    if (key == QLatin1String("tile")) {
        tile = std::clamp(value.toInt(), 0, 255);
    } else if (key == QLatin1String("palette")) {
        // The whole label: a translation may place the number anywhere.
        int palette = 0;
        for (int p = 0; p < 4; ++p) {
            if (value.toString() == tr("SPR %1").arg(p)) palette = p;
        }
        attributes = (attributes & ~3) | palette;
    } else if (key == QLatin1String("flip")) {
        static const QStringList kFlips{QStringLiteral("None"), tr("Horizontal"), tr("Vertical"),
                                        tr("Both")};
        const int flip = std::max(0, int(kFlips.indexOf(value.toString())));
        attributes = (attributes & ~0xc0) | ((flip & 1) ? 0x40 : 0) | ((flip & 2) ? 0x80 : 0);
    } else {
        return CompositionModel::setField(key, value, error);
    }
    return setDescriptor(selected_, tile, attributes, error);
}

sdk::Library CastleExcellentAnimationModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.cell = QSize(1, 2);
    result.hint = tr("drag a pattern pair onto a sprite");
    const int palette = selected_ >= 0 ? attributesOf(selected_) & 3 : 0;
    const CastleExcellentPainter& art = session_->painter();
    for (int table = 0; table < 2; ++table) {
        for (int pair = 0; pair < 128; ++pair) {
            const int tile = pair * 2 + table;
            QImage icon(8, 16, QImage::Format_ARGB32);
            icon.fill(art.backdrop());
            QPainter p(&icon);
            p.drawImage(0, 0, art.sprite(CastleExcellentSprite{tile, palette, 0, 0}));
            p.end();
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = nes::hex(tile);
            item.icon = icon;
            result.items << item;
        }
    }
    return result;
}

sdk::Inspection CastleExcellentAnimationModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("castle-excellent.metasprite-descriptors.v1");
    if (selected_ < 0) {
        inspection.kicker = animation().group == QLatin1String("Player") ? tr("Player animation")
                                                                         : tr("Object frames");
        inspection.title = title();
        sdk::Field count;
        count.key = QStringLiteral("frames");
        count.label = tr("Frames");
        count.value = QString::number(animation().frames.size());
        inspection.fields = {count};
        inspection.note = animation().group == QLatin1String("Player")
                              ? tr("The frames the game shows the player in, as its frame selection steps "
                                   "them. Select a sprite to change its patterns, palette or flip, or drop a "
                                   "pattern pair from the sheet on it.")
                              : tr("The frames this object's sprite selector chooses between (by direction "
                                   "and the frame counter), played here one after another. Select a sprite "
                                   "to edit it. Frames share sprites with other objects where the game does.");
        return inspection;
    }
    const int attributes = attributesOf(selected_);
    inspection.kicker = tr("Sprite");
    inspection.title = tr("Sprite %1").arg(selected_);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Patterns");
    tile.type = sdk::Field::Type::Integer;
    tile.value = tileOf(selected_);
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = tr("%1 · table %2").arg(nes::hex(tileOf(selected_))).arg(tileOf(selected_) & 1);
    tile.monospace = true;
    sdk::Field palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Palette");
    palette.type = sdk::Field::Type::Choice;
    for (int p = 0; p < 4; ++p) {
        palette.choices << tr("SPR %1").arg(p);
        palette.choiceIcons << session_->painter().sprite(CastleExcellentSprite{tileOf(selected_), (attributes & ~3) | p, 0, 0});
    }
    palette.value = tr("SPR %1").arg(attributes & 3);
    sdk::Field flip;
    flip.key = QStringLiteral("flip");
    flip.label = tr("Flip");
    flip.type = sdk::Field::Type::Choice;
    flip.choices = QStringList{QStringLiteral("None"), tr("Horizontal"), tr("Vertical"),
                               tr("Both")};
    flip.value = flip.choices.at(((attributes & 0x40) ? 1 : 0) | ((attributes & 0x80) ? 2 : 0));
    inspection.fields = {tile, palette, flip};
    inspection.note = tr("An 8x16 sprite: a pattern pair (even from the first pattern table, odd from the "
                         "second) and its attributes. Every frame and object that draws this sprite "
                         "changes with it.");
    return inspection;
}

QString CastleExcellentAnimationModel::statusText() const {
    return tr("Animations · %1 · frame %2").arg(title()).arg(frame_ + 1);
}

}  // namespace rl::castle_excellent
