#include "flicky/plugin/bonus_model.hpp"

#include "flicky/plugin/flicky_session.hpp"
#include "flicky/plugin/objects.hpp"
#include "sdk/i18n.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::flicky {

namespace {

constexpr int kWorldMin = 0x80;
constexpr int kWorldMax = 0x17F;
const QStringList kPlacements{QStringLiteral("seesaw"), QStringLiteral("tiger"), QStringLiteral("chick_spawn")};

QString two(int value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

QString hex(int value) { return QStringLiteral("$%1").arg(value, 3, 16, QLatin1Char('0')).toUpper(); }

QString placementTitle(const QString& id) {
    if (id == QLatin1String("seesaw")) return rlTr("flicky", "Seesaw");
    if (id == QLatin1String("tiger")) return rlTr("flicky", "Tiger");
    return rlTr("flicky", "Chick spawn");
}

// The animations whose first frame shows each actor, for its bounds.
QString animationOf(const QString& id) {
    if (id == QLatin1String("seesaw")) return QStringLiteral("BonusSeesaw_Anim");
    if (id == QLatin1String("tiger")) return QStringLiteral("BonusTiger_Anim");
    return {};
}

// The data index is not a screen side: seesaw and tiger list their right-hand
// copy first, the chick spawns their left one. Name each by where the game
// places it.
QString sideName(const QJsonObject& placement, int side) {
    const int x = placement.value(QStringLiteral("baseline_x")).toArray().at(side).toInt();
    return x < 0x100 ? QStringLiteral("left") : QStringLiteral("right");
}

int parseWorld(const QString& text, bool* ok) {
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('$'))) {
        return value.mid(1).toInt(ok, 16);
    }
    if (value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        return value.mid(2).toInt(ok, 16);
    }
    return value.toInt(ok, 10);
}

}  // namespace

BonusRoundModel::BonusRoundModel(FlickySession* session, int round)
    : TileMapModel(session), session_(session), round_(round) {
    connect(session_, &FlickySession::levelsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::graphicsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::semanticsChanged, this, &sdk::EditorModel::contentChanged);
    connect(session_, &FlickySession::sequencesChanged, this, &sdk::EditorModel::contentChanged);
}

BonusRoundModel::Actor BonusRoundModel::parseId(const QString& id) {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("bonus") || !kPlacements.contains(parts.at(1))) {
        return {};
    }
    return Actor{parts.at(1), parts.at(2).toInt()};
}

QString BonusRoundModel::title() const { return tr("Round %1").arg(two(round_ + 1)); }

QSize BonusRoundModel::gridSize() const { return {objects::kMapWidth, objects::kMapHeight}; }

QVector<sdk::Tool> BonusRoundModel::tools() const {
    return {{QStringLiteral("select"), tr("Select and move"), QStringLiteral("select")}};
}

QImage BonusRoundModel::render() const {
    try {
        return session_->renderer().renderBonusPhase(session_->levels(), round_, session_->bonusPhase());
    } catch (const std::exception& e) {
        QImage image(256, 224, QImage::Format_RGB32);
        image.fill(QColor(0x20, 0x16, 0x18));
        QPainter painter(&image);
        painter.setPen(QColor(0xff, 0xb4, 0xa8));
        painter.drawText(image.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                         QString::fromUtf8(e.what()));
        return image;
    }
}

QPoint BonusRoundModel::anchor(const Actor& actor) const {
    const QJsonObject p = session_->levels().bonusPlacement(actor.placement);
    const int x = p.value(QStringLiteral("x")).toArray().at(actor.side).toInt();
    const int y = p.value(QStringLiteral("y")).toInt();
    return QPoint(x - kWorldMin, y - kWorldMin);
}

QRect BonusRoundModel::bounds(const Actor& actor) const {
    const QString animation = animationOf(actor.placement);
    if (animation.isEmpty()) {
        return QRect(-8, -8, 16, 16);
    }
    try {
        const QJsonArray frames = session_->sequences().animation(animation).value(QStringLiteral("frames")).toArray();
        const QJsonArray pieces =
            session_->sequences().mapping(frames.first().toString()).value(QStringLiteral("pieces")).toArray();
        QRect box;
        for (const QJsonValue& value : pieces) {
            const QJsonObject piece = value.toObject();
            // Side 1 draws mirrored, from the flipped X of each piece.
            const int x = piece.value(actor.side == 1 ? QStringLiteral("flipped_x") : QStringLiteral("x")).toInt();
            const QRect r(x, piece.value(QStringLiteral("y")).toInt(), piece.value(QStringLiteral("width")).toInt() * 8,
                          piece.value(QStringLiteral("height")).toInt() * 8);
            box = box.isNull() ? r : box.united(r);
        }
        return box.isNull() ? QRect(-8, -8, 16, 16) : box;
    } catch (const std::exception&) {
        return QRect(-8, -8, 16, 16);
    }
}

QVector<sdk::MapObject> BonusRoundModel::objects() const {
    QVector<sdk::MapObject> result;
    for (const QString& id : kPlacements) {
        for (int side = 0; side < 2; ++side) {
            const Actor actor{id, side};
            const QRect pixels = bounds(actor).translated(anchor(actor));
            sdk::MapObject object;
            object.id = QStringLiteral("bonus:%1:%2").arg(id).arg(side);
            object.label = QStringLiteral("%1 · %2").arg(placementTitle(id),
                                                         sideName(session_->levels().bonusPlacement(id), side));
            // Cells covering the sprite.
            const int left = pixels.left() >= 0 ? pixels.left() / 8 : (pixels.left() - 7) / 8;
            const int top = pixels.top() >= 0 ? pixels.top() / 8 : (pixels.top() - 7) / 8;
            const int right = pixels.right() / 8;
            const int bottom = pixels.bottom() / 8;
            object.cells = QRect(QPoint(left, top), QPoint(right, bottom));
            result.append(object);
        }
    }
    return result;
}

void BonusRoundModel::selectObject(const QString& id) {
    if (id != selected_) {
        selected_ = id;
        emit selectionChanged();
    }
}

bool BonusRoundModel::place(const Actor& actor, int worldX, int worldY, QString* error, bool live) {
    worldX = std::clamp(worldX, kWorldMin, kWorldMax);
    worldY = std::clamp(worldY, kWorldMin, kWorldMax);
    const auto edit = [actor, worldX, worldY](FlickyLevelDocument& levels) {
        levels.setBonusPlacement(actor.placement, actor.side, worldX);
        levels.setBonusPlacementY(actor.placement, worldY);
    };
    if (live) {
        FlickyLevelDocument next = session_->levels();
        try {
            edit(next);
        } catch (const std::exception& e) {
            if (error) *error = QString::fromUtf8(e.what());
            return false;
        }
        session_->setLevelsLive(next);
        return true;
    }
    return session_->editLevels(tr("Move %1").arg(placementTitle(actor.placement)), edit, error);
}

void BonusRoundModel::beginDrag(const QString& id) {
    const Actor actor = parseId(id);
    if (!actor.isValid()) {
        return;
    }
    before_ = session_->levels();
    // Keep the grab point: objects are dragged by their top-left cell.
    const QRect pixels = bounds(actor).translated(anchor(actor));
    const int left = pixels.left() >= 0 ? pixels.left() / 8 : (pixels.left() - 7) / 8;
    const int top = pixels.top() >= 0 ? pixels.top() / 8 : (pixels.top() - 7) / 8;
    dragOffset_ = anchor(actor) - QPoint(left * 8, top * 8);
    selectObject(id);
}

bool BonusRoundModel::dragObject(const QString& id, QPoint topLeftCell) {
    const Actor actor = parseId(id);
    if (!before_ || !actor.isValid()) {
        return false;
    }
    const QPoint screen = topLeftCell * 8 + dragOffset_;
    const QPoint current = anchor(actor);
    if (screen == current) {
        return false;
    }
    return place(actor, screen.x() + kWorldMin, screen.y() + kWorldMin, nullptr, true);
}

void BonusRoundModel::endDrag() {
    const Actor actor = parseId(selected_);
    if (before_ && actor.isValid()) {
        const QJsonObject was = before_->bonusPlacement(actor.placement);
        const QJsonObject now = session_->levels().bonusPlacement(actor.placement);
        if (was != now) {
            session_->commitLevels(*before_, session_->levels(), tr("Move %1").arg(placementTitle(actor.placement)));
        }
    }
    before_.reset();
}

QVector<sdk::ViewOption> BonusRoundModel::viewOptions() const {
    sdk::ViewOption phase;
    phase.key = QStringLiteral("phase");
    phase.label = tr("Frame");
    phase.value = QString::number(session_->bonusPhase());
    phase.choices =
        QStringList{QStringLiteral("11"),  QStringLiteral("17"),  QStringLiteral("30"),  QStringLiteral("60"),
                    QStringLiteral("120"), QStringLiteral("240"), QStringLiteral("400"), QStringLiteral("600")};
    return {phase};
}

void BonusRoundModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("phase")) {
        session_->setBonusPhase(std::clamp(value.toInt(), 1, 600));
        emit contentChanged();
        emit toolsChanged();
    }
}

sdk::Inspection BonusRoundModel::inspect() const {
    sdk::Inspection inspection;
    inspection.engineTag = QStringLiteral("flicky.levels.v1");
    const Actor actor = parseId(selected_);
    if (!actor.isValid()) {
        inspection.kicker = tr("Bonus round");
        inspection.title = title();
        sdk::Field phase;
        phase.key = QStringLiteral("phase");
        phase.label = tr("Frame");
        phase.type = sdk::Field::Type::Integer;
        phase.minimum = 1;
        phase.maximum = 600;
        phase.value = session_->bonusPhase();
        phase.hint = QStringLiteral("1–600");
        inspection.fields = {phase};
        inspection.note =
            tr("The scene is simulated frame by frame after the round becomes "
               "ready, without player input. Pick a seesaw, tiger or the chick "
               "spawn to move it; every bonus round shares these placements.");
        inspection.engine = {
            {tr("Source"), QStringLiteral("game/bonus/objects.s")},
            {tr("Changed"), session_->bonusPlacementsDirty() ? tr("since save") : QStringLiteral("no")}};
        return inspection;
    }
    const QJsonObject p = session_->levels().bonusPlacement(actor.placement);
    const int x = p.value(QStringLiteral("x")).toArray().at(actor.side).toInt();
    const int y = p.value(QStringLiteral("y")).toInt();
    inspection.kicker = tr("Bonus actor");
    inspection.title = QStringLiteral("%1 · %2").arg(placementTitle(actor.placement), sideName(p, actor.side));
    sdk::Field fx;
    fx.key = QStringLiteral("x");
    fx.label = QStringLiteral("X");
    fx.type = sdk::Field::Type::Text;
    fx.value = hex(x);
    fx.hint = QStringLiteral("$080–$17F");
    fx.monospace = true;
    sdk::Field fy = fx;
    fy.key = QStringLiteral("y");
    fy.label = QStringLiteral("Y");
    fy.value = hex(y);
    sdk::Field screen;
    screen.key = QStringLiteral("screen");
    screen.label = tr("On screen");
    screen.value = tr("%1, %2 px").arg(x - kWorldMin).arg(y - kWorldMin);
    screen.monospace = true;
    inspection.fields = {fx, fy, screen};
    inspection.note =
        tr("X is set per side; Y is shared by both sides. Coordinates are "
           "sprite space: $080 is the screen's left or top edge.");
    const QJsonArray baselineX = p.value(QStringLiteral("baseline_x")).toArray();
    inspection.engine = {
        {tr("Placement"), actor.placement},
        {tr("Source"), p.value(QStringLiteral("source")).toString()},
        {tr("Original"), QStringLiteral("%1, %2").arg(hex(baselineX.at(actor.side).toInt()),
                                                      hex(p.value(QStringLiteral("baseline_y")).toInt()))}};
    return inspection;
}

bool BonusRoundModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("phase")) {
        setViewOption(key, value.toString());
        return true;
    }
    const Actor actor = parseId(selected_);
    if (!actor.isValid() || (key != QLatin1String("x") && key != QLatin1String("y"))) {
        return TileMapModel::setField(key, value, error);
    }
    bool ok = false;
    const int world = parseWorld(value.toString(), &ok);
    if (!ok || world < kWorldMin || world > kWorldMax) {
        if (error) *error = tr("Use $080–$17F (or 128–383).");
        return false;
    }
    const QJsonObject p = session_->levels().bonusPlacement(actor.placement);
    const int x = key == QLatin1String("x") ? world : p.value(QStringLiteral("x")).toArray().at(actor.side).toInt();
    const int y = key == QLatin1String("y") ? world : p.value(QStringLiteral("y")).toInt();
    return place(actor, x, y, error, false);
}

QString BonusRoundModel::startPoint() const { return tr("Round %1").arg(two(round_ + 1)); }

QString BonusRoundModel::statusText() const {
    const Actor actor = parseId(selected_);
    if (!actor.isValid()) {
        return tr("%1 · bonus round · frame %2").arg(title()).arg(session_->bonusPhase());
    }
    const QPoint a = anchor(actor);
    return tr("%1 at %2, %3 px · frame %4")
        .arg(placementTitle(actor.placement))
        .arg(a.x())
        .arg(a.y())
        .arg(session_->bonusPhase());
}

}  // namespace rl::flicky
