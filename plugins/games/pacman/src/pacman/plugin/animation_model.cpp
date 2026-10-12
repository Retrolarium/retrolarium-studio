#include "pacman/plugin/animation_model.hpp"

#include "nes/editors.hpp"
#include "pacman/plugin/pacman_session.hpp"
#include "sdk/game_names.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>

namespace rl::pacman {

namespace {

constexpr int kStandardFrames = 64;
constexpr int kAlternateFrames = 13;

PacmanAnimation animation(const char* id, const char* label, const char* group, QVector<PacmanFrame> frames,
                          int palette = 0) {
    PacmanAnimation a;
    a.id = QString::fromLatin1(id);
    a.label = QString::fromLatin1(label);
    a.group = QString::fromLatin1(group);
    a.frames = std::move(frames);
    a.palette = palette;
    return a;
}

QVector<PacmanFrame> run(int first, int last, int ticks, bool alternate = false) {
    QVector<PacmanFrame> frames;
    for (int i = first; i <= last; ++i) frames << PacmanFrame{i, alternate, ticks};
    return frames;
}

// sub_update_pacman_anim_frame: eight steps of the phase counter show the
// direction's open frame three times, wide twice, open once, closed twice.
PacmanAnimation chomp(const char* id, const char* label, int open) {
    return animation(id, label, "Pac-Man", {{open, false, 3}, {open + 1, false, 2}, {open, false, 1}, {1, false, 2}});
}

PacmanAnimation ghost(const char* id, const char* label, int first) {
    // The two frames alternate with bit 3 of the frame counter.
    PacmanAnimation a = animation(id, label, "Ghosts", run(first, first + 1, 8));
    a.ghost = true;
    return a;
}

}  // namespace

QString ghostName(int palette) {
    const int ghost = palette & 3;
    return sdk::gameName("ghosts", ghost, rlTr("pacman", "Ghost %1").arg(ghost + 1));
}

QString animationLabel(const PacmanAnimation& animation) {
    if (animation.fruitIndex < 0) return animation.label;
    return sdk::gameName("fruits", animation.fruitIndex,
                         rlTr("pacman", "Fruit %1").arg(animation.fruitIndex + 1));
}

const QVector<PacmanAnimation>& pacmanAnimations() {
    // The frame map of the source reconstruction (actor-frames): directions
    // follow the engine's order up, left, down, right.
    static const QVector<PacmanAnimation> kAnimations = [] {
        QVector<PacmanAnimation> list;
        list << chomp("pacman:up", "Up", 2) << chomp("pacman:left", "Left", 4) << chomp("pacman:down", "Down", 6)
             << chomp("pacman:right", "Right", 8);
        list << animation("pacman:death", "Death", "Pac-Man", run(18, 28, 8));
        list << ghost("ghost:up", "Up", 10) << ghost("ghost:left", "Left", 12) << ghost("ghost:down", "Down", 14)
             << ghost("ghost:right", "Right", 16);
        PacmanAnimation frightened = animation("ghost:frightened", "Frightened", "Ghosts", run(30, 31, 8), 1);
        frightened.frightened = true;
        list << frightened;
        PacmanAnimation eyes = animation("ghost:eyes", "Eyes going home", "Ghosts", run(32, 35, 16));
        eyes.ghost = true;
        list << eyes;
        // The eight fruits, named by the game (animationLabel).
        for (int f = 0; f < 8; ++f) {
            PacmanAnimation fruit = animation("fruit", "Fruit", "Fruit", {{36 + f, false, 8}}, 3);
            fruit.id = QStringLiteral("fruit:%1").arg(f);
            fruit.fruitIndex = f;
            fruit.fruit = true;
            list << fruit;
        }
        PacmanAnimation scores = animation("scores", "Scores", "Scores", run(44, 55, 30), 0);
        list << scores;
        list << animation("big", "Big Pac-Man pieces", "Intermission", run(56, 63, 15));
        list << animation("alternate", "Intermission pieces", "Intermission", run(0, kAlternateFrames - 1, 15, true));
        QVector<PacmanFrame> all = run(0, kStandardFrames - 1, 10);
        all += run(0, kAlternateFrames - 1, 10, true);
        list << animation("all", "All frames", "Frames", all);
        return list;
    }();
    return kAnimations;
}

PacmanAnimationModel::PacmanAnimationModel(PacmanSession* session, int animation)
    : CompositionModel(session), session_(session), index_(animation) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

const PacmanAnimation& PacmanAnimationModel::animation() const {
    return pacmanAnimations().at(index_);
}

const PacmanFrame& PacmanAnimationModel::current() const {
    return animation().frames.at(std::clamp(frame_, 0, int(animation().frames.size()) - 1));
}

QString PacmanAnimationModel::title() const {
    const PacmanAnimation& a = animation();
    if (a.group == QLatin1String("Pac-Man") || a.group == QLatin1String("Ghosts")) {
        return QStringLiteral("%1 · %2").arg(a.group == QLatin1String("Ghosts") ? tr("Ghost") : a.group, a.label);
    }
    return animationLabel(a);
}

int PacmanAnimationModel::palette() const {
    return animation().ghost ? ghost_ : animation().palette;
}

QVector<QVector<int>> PacmanAnimationModel::sprites() const {
    // The intermissions load their own sprite colours.
    const bool intermission = animation().group == QLatin1String("Intermission") || current().alternate;
    QVector<QVector<int>> result =
        session_->painter().spritePalettes(intermission ? QStringLiteral("intermission-sprites") : ids::kGameplay);
    if (animation().frightened) {
        // Every frightened ghost: sprite palette 1 with the frightened
        // colour in entry 1 ($3F15).
        QVector<int> palette = result.at(1);
        palette[1] = session_->bundle().tableValues(QStringLiteral("frightened-active-normal")).value(0, 0x11);
        result = {palette, palette, palette, palette};
    }
    if (animation().fruit) {
        // The round's fruit colour goes into the fruit palette's entry 1.
        const QVector<int> colours = session_->bundle().tableValues(QStringLiteral("fruit-by-stage"));
        const int at = std::min(session_->startRound() - 1, 15);
        if (at < colours.size()) result[3][1] = colours.at(at);
    }
    return result;
}

int PacmanAnimationModel::tileOf(int quarter) const {
    const PacmanFrame& f = current();
    return session_->resource(ids::kActors)
        .value(f.alternate ? QStringLiteral("alternate_tile_quads") : QStringLiteral("standard_tile_quads"))
        .toArray().at(f.index).toArray().at(quarter).toInt();
}

int PacmanAnimationModel::attributesOf(int quarter) const {
    const PacmanFrame& f = current();
    return session_->resource(ids::kActors)
        .value(f.alternate ? QStringLiteral("alternate_attribute_quads") : QStringLiteral("standard_attribute_quads"))
        .toArray().at(f.index).toArray().at(quarter).toInt();
}

QImage PacmanAnimationModel::render() const {
    QImage image(canvasSize(), QImage::Format_ARGB32);
    image.fill(session_->painter().color(session_->bundle().tableValues(ids::kGameplay).value(0, 0x0f)));
    QPainter painter(&image);
    const PacmanFrame& f = current();
    painter.drawImage(origin() - QPoint(8, 8),
                      session_->painter().frame(session_->resource(ids::kActors), f.alternate, f.index, palette(), sprites()));
    return image;
}

QVector<sdk::CompositionPiece> PacmanAnimationModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    for (int q = 0; q < 4; ++q) {
        sdk::CompositionPiece piece;
        piece.id = QStringLiteral("q:%1").arg(q);
        piece.label = tr("Sprite %1 · %2").arg(q + 1).arg(nes::hex(tileOf(q)));
        piece.pixels = QRect(origin() - QPoint(8, 8) + QPoint((q % 2) * 8, (q / 2) * 8), QSize(8, 8));
        piece.movable = false;
        result << piece;
    }
    return result;
}

QString PacmanAnimationModel::selectedPiece() const {
    return selected_ >= 0 ? QStringLiteral("q:%1").arg(selected_) : QString();
}

void PacmanAnimationModel::selectPiece(const QString& id) {
    const int quarter = id.startsWith(QLatin1String("q:")) ? std::clamp(id.mid(2).toInt(), 0, 3) : -1;
    if (quarter != selected_) {
        selected_ = quarter;
        emit selectionChanged();
    }
}

QVector<sdk::CompositionFrame> PacmanAnimationModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QVector<PacmanFrame>& list = animation().frames;
    for (int i = 0; i < list.size(); ++i) {
        sdk::CompositionFrame f;
        f.id = QString::number(i);
        f.label = list.at(i).alternate ? tr("Alt %1").arg(list.at(i).index)
                                       : tr("Frame %1").arg(list.at(i).index);
        f.detail = tr("%n ticks", nullptr, list.at(i).ticks);
        result << f;
    }
    return result;
}

void PacmanAnimationModel::setCurrentFrame(int frame) {
    if (frame != frame_ && frame >= 0 && frame < animation().frames.size()) {
        frame_ = frame;
        emit selectionChanged();
        emit contentChanged();
    }
}

int PacmanAnimationModel::frameDurationMs(int frame) const {
    return animation().frames.value(frame).ticks * 1000 / 60;
}

QVector<sdk::ViewOption> PacmanAnimationModel::viewOptions() const {
    if (!animation().ghost) return {};
    sdk::ViewOption option;
    option.key = QStringLiteral("ghost");
    option.label = tr("Ghost");
    for (int g = 0; g < 4; ++g) option.choices << ghostName(g);
    option.value = ghostName(ghost_);
    return {option};
}

void PacmanAnimationModel::setViewOption(const QString& key, const QString& value) {
    if (key != QLatin1String("ghost")) return;
    for (int g = 0; g < 4; ++g) {
        if (ghostName(g) == value && g != ghost_) {
            ghost_ = g;
            emit toolsChanged();
            emit contentChanged();
        }
    }
}

bool PacmanAnimationModel::setQuarter(int quarter, int tile, int attributes, QString* error) {
    const PacmanFrame f = current();
    return session_->editBundle(
        tr("Edit %1 frame %2").arg(f.alternate ? QStringLiteral("alternate") : QStringLiteral("actor")).arg(f.index),
        [f, quarter, tile, attributes](nes::Bundle& b) {
            QJsonObject actors = b.resource(ids::kActors);
            const QString tilesKey = f.alternate ? QStringLiteral("alternate_tile_quads") : QStringLiteral("standard_tile_quads");
            const QString attributesKey =
                f.alternate ? QStringLiteral("alternate_attribute_quads") : QStringLiteral("standard_attribute_quads");
            QJsonArray tiles = actors.value(tilesKey).toArray();
            QJsonArray quad = tiles.at(f.index).toArray();
            quad.replace(quarter, tile);
            tiles.replace(f.index, quad);
            QJsonArray attrs = actors.value(attributesKey).toArray();
            QJsonArray attrQuad = attrs.at(f.index).toArray();
            attrQuad.replace(quarter, attributes);
            attrs.replace(f.index, attrQuad);
            actors.insert(tilesKey, tiles);
            actors.insert(attributesKey, attrs);
            b.setResource(ids::kActors, actors);
        },
        error);
}

bool PacmanAnimationModel::dropItem(const QString& libraryItem, const QString& piece, QString* error) {
    const int quarter = piece.startsWith(QLatin1String("q:")) ? piece.mid(2).toInt() : -1;
    if (!libraryItem.startsWith(QLatin1String("tile:")) || quarter < 0) {
        if (error) *error = tr("Drop a sprite pattern onto a quarter of the frame.");
        return false;
    }
    selectPiece(piece);
    return setQuarter(quarter, std::clamp(libraryItem.mid(5).toInt(), 0, 255), attributesOf(quarter), error);
}

bool PacmanAnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (selected_ < 0) return CompositionModel::setField(key, value, error);
    int tile = tileOf(selected_);
    int attributes = attributesOf(selected_);
    if (key == QLatin1String("tile")) {
        tile = std::clamp(value.toInt(), 0, 255);
    } else if (key == QLatin1String("palette")) {
        attributes = (attributes & ~3) | std::clamp(value.toString().section(QLatin1Char(' '), 1).toInt(), 0, 3);
    } else if (key == QLatin1String("priority")) {
        attributes = (attributes & ~0x20) | (value.toString() == QLatin1String("Behind the maze") ? 0x20 : 0);
    } else if (key == QLatin1String("flip")) {
        static const QStringList kFlips{QStringLiteral("None"), tr("Horizontal"), tr("Vertical"),
                                        tr("Both")};
        const int flip = std::max(0, int(kFlips.indexOf(value.toString())));
        attributes = (attributes & ~0xc0) | ((flip & 1) ? 0x40 : 0) | ((flip & 2) ? 0x80 : 0);
    } else {
        return CompositionModel::setField(key, value, error);
    }
    return setQuarter(selected_, tile, attributes, error);
}

sdk::Library PacmanAnimationModel::sheet() const {
    sdk::Library result;
    result.grid = true;
    result.columns = 16;
    result.hint = tr("drag a pattern onto a quarter");
    const QVector<QVector<int>> palettes = sprites();
    const int p = selected_ >= 0 ? (palette() | (attributesOf(selected_) & 3)) & 3 : palette();
    const QColor backdrop = session_->painter().color(palettes.at(p).value(0, 0x0f));
    for (int tile = 0; tile < 256; ++tile) {
        QImage icon(8, 8, QImage::Format_ARGB32);
        icon.fill(backdrop);
        QPainter painter(&icon);
        painter.drawImage(0, 0, session_->painter().tile(256 + tile, palettes.at(p), true));
        painter.end();
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(tile);
        item.label = nes::hex(tile);
        item.icon = icon;
        result.items << item;
    }
    return result;
}

sdk::Inspection PacmanAnimationModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("pacman.actor-mapping.v1");
    const PacmanFrame& f = current();
    if (selected_ < 0) {
        inspection.kicker = animation().group == QLatin1String("Frames") ? tr("Actor frames")
                                                                         : tr("Animation");
        inspection.title = title();
        sdk::Field count;
        count.key = QStringLiteral("frames");
        count.label = tr("Frames");
        count.value = QString::number(animation().frames.size());
        sdk::Field shown;
        shown.key = QStringLiteral("frame");
        shown.label = tr("Shown");
        shown.value = f.alternate ? tr("alternate frame %1").arg(f.index)
                                  : tr("actor frame %1").arg(f.index);
        inspection.fields = {count, shown};
        QString note = tr("A frame is four 8x8 sprites around the actor's position. Select one to change "
                          "its pattern, palette, priority or flip, or drop a pattern from the sheet on it. "
                          "Frames are shared: every animation that shows one changes with it.");
        if (animation().ghost) note += tr(" Ghost shows the frames in each ghost's colours.");
        if (animation().fruit) {
            note += tr(" The fruit's colour is the one Fruit by stage gives round %1 (the start round).")
                        .arg(session_->startRound());
        }
        inspection.note = note;
        return inspection;
    }
    const int attributes = attributesOf(selected_);
    inspection.kicker = tr("Sprite");
    inspection.title = tr("%1 · sprite %2").arg(frames().value(frame_).label).arg(selected_ + 1);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Pattern");
    tile.type = sdk::Field::Type::Integer;
    tile.value = tileOf(selected_);
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = tr("%1 · sprite table").arg(nes::hex(tileOf(selected_)));
    tile.monospace = true;
    sdk::Field pal;
    pal.key = QStringLiteral("palette");
    pal.label = tr("Palette bits");
    pal.type = sdk::Field::Type::Choice;
    const QVector<QVector<int>> palettes = sprites();
    for (int p = 0; p < 4; ++p) {
        pal.choices << tr("SPR %1").arg(p);
        pal.choiceIcons << session_->painter().tile(256 + tileOf(selected_), palettes.at((palette() | p) & 3));
    }
    pal.value = tr("SPR %1").arg(attributes & 3);
    pal.hint = tr("ORed with the actor's");
    sdk::Field priority;
    priority.key = QStringLiteral("priority");
    priority.label = tr("Priority");
    priority.type = sdk::Field::Type::Choice;
    priority.choices = QStringList{tr("In front"), QStringLiteral("Behind the maze")};
    priority.value = priority.choices.at((attributes & 0x20) ? 1 : 0);
    sdk::Field flip;
    flip.key = QStringLiteral("flip");
    flip.label = tr("Flip");
    flip.type = sdk::Field::Type::Choice;
    flip.choices = QStringList{QStringLiteral("None"), tr("Horizontal"), tr("Vertical"),
                               tr("Both")};
    flip.value = flip.choices.at(((attributes & 0x40) ? 1 : 0) | ((attributes & 0x80) ? 2 : 0));
    inspection.fields = {tile, pal, priority, flip};
    inspection.note = tr("The game ORs the actor's own palette into these bits: 0 for Pac-Man and "
                         "%1, 1 for %2, 2 for %3, 3 for %4 and the fruit.")
                          .arg(ghostName(0), ghostName(1), ghostName(2), ghostName(3));
    return inspection;
}

QString PacmanAnimationModel::statusText() const {
    return tr("Animations · %1 · %2").arg(title(), frames().value(frame_).label);
}

}  // namespace rl::pacman
