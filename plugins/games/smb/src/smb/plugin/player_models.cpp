#include "smb/plugin/player_models.hpp"

#include "smb/plugin/smb_session.hpp"

#include <QJsonArray>

namespace rl::smb {

namespace {

constexpr int kTilesPerFrame = 8;

QString hex(int value) {
    return QLatin1Char('$') + QStringLiteral("%1").arg(value & 0xff, 2, 16, QLatin1Char('0')).toUpper();
}

// "big_walk_1" -> "Big walk 1"
QString frameLabel(QString name) {
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

QStringList frameNames(const SmbResourceDocument& d, const QString& id) {
    QStringList names;
    for (const QJsonValue& v : d.resource(id).value(QStringLiteral("frame_names")).toArray()) {
        names << v.toString();
    }
    return names;
}

QVector<int> intsOf(const QJsonValue& value) {
    QVector<int> result;
    for (const QJsonValue& v : value.toArray()) result << v.toInt();
    return result;
}

// The action slots of the frame-offset table, as the engine indexes them.
const QStringList& actionNames() {
    static const QStringList names{
        QStringLiteral("Big · jump"),   QStringLiteral("Big · swim"),   QStringLiteral("Big · stand"),
        QStringLiteral("Big · skid"),   QStringLiteral("Big · walk"),   QStringLiteral("Big · climb"),
        QStringLiteral("Big · crouch"), QStringLiteral("Big · throw"),  QStringLiteral("Small · jump"),
        QStringLiteral("Small · swim"), QStringLiteral("Small · stand"), QStringLiteral("Small · skid"),
        QStringLiteral("Small · walk"), QStringLiteral("Small · climb"), QStringLiteral("Small · killed"),
        QStringLiteral("Small · throw slot")};
    return names;
}

}  // namespace

// ---------------------------------------------------------------- frames

SmbPlayerFramesModel::SmbPlayerFramesModel(SmbSession* session) : CompositionModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<int> SmbPlayerFramesModel::tiles(int frame) const {
    return session_->document().playerFrameTiles(session_->playerFrames(), frame);
}

QVector<QRgb> SmbPlayerFramesModel::colors() const {
    const QVector<int> values = session_->document().tableValues(session_->playerPalette());
    QVector<QRgb> result;
    for (int i = 0; i < 4; ++i) {
        result << session_->painter().nesColor(values.value(palette_ * 4 + i));
    }
    return result;
}

QImage SmbPlayerFramesModel::render() const {
    QImage image(16, 32, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const QVector<int> t = tiles(frame_);
    const int blank = session_->document().playerBlankTile(session_->playerFrames());
    const int base = session_->document().playerTileBase(session_->playerFrames());
    const QVector<QRgb> pens = colors();
    for (int i = 0; i < t.size() && i < kTilesPerFrame; ++i) {
        if (t.at(i) == blank) {
            continue;
        }
        const QPoint at((i & 1) * 8, (i / 2) * 8);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int pixel = session_->document().pixel(session_->chr(), base + t.at(i), x, y);
                if (pixel != 0) {
                    image.setPixel(at.x() + x, at.y() + y, pens.value(pixel));
                }
            }
        }
    }
    return image;
}

QVector<sdk::ViewOption> SmbPlayerFramesModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = QStringLiteral("Colours");
    const QStringList names{QStringLiteral("Hero"), QStringLiteral("Brother"), QStringLiteral("Fire")};
    palette.value = names.value(palette_);
    const int rows = session_->document().tableValues(session_->playerPalette()).size() / 4;
    palette.choices = names.mid(0, std::max(1, std::min(rows, int(names.size()))));
    return {palette};
}

void SmbPlayerFramesModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        palette_ = std::max(0, QStringList{QStringLiteral("Hero"), QStringLiteral("Brother"), QStringLiteral("Fire")}.indexOf(value));
        emit contentChanged();
        emit selectionChanged();
    }
}

QVector<sdk::CompositionPiece> SmbPlayerFramesModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const QVector<int> t = tiles(frame_);
    for (int i = 0; i < t.size() && i < kTilesPerFrame; ++i) {
        sdk::CompositionPiece piece;
        piece.id = QStringLiteral("tile:%1").arg(i);
        piece.label = hex(t.at(i));
        piece.pixels = QRect((i & 1) * 8, (i / 2) * 8, 8, 8);
        piece.movable = false;
        result << piece;
    }
    return result;
}

QString SmbPlayerFramesModel::selectedPiece() const {
    return piece_ >= 0 ? QStringLiteral("tile:%1").arg(piece_) : QString();
}

void SmbPlayerFramesModel::selectPiece(const QString& id) {
    const int piece = id.startsWith(QLatin1String("tile:")) ? id.mid(5).toInt() : -1;
    if (piece != piece_) {
        piece_ = piece;
        emit selectionChanged();
    }
}

QVector<sdk::CompositionFrame> SmbPlayerFramesModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QStringList names = frameNames(session_->document(), session_->playerFrames());
    for (int i = 0; i < names.size(); ++i) {
        sdk::CompositionFrame frame;
        frame.id = names.at(i);
        frame.label = frameLabel(names.at(i));
        frame.detail = QStringLiteral("frame %1").arg(i);
        result << frame;
    }
    return result;
}

void SmbPlayerFramesModel::setCurrentFrame(int frame) {
    if (frame != frame_ && frame >= 0 && frame < frames().size()) {
        frame_ = frame;
        emit contentChanged();
        emit selectionChanged();
    }
}

int SmbPlayerFramesModel::frameDurationMs(int) const {
    return 133;     // about eight frames at 60 Hz, a walking pace
}

QString SmbPlayerFramesModel::currentLibraryItem() const {
    return piece_ >= 0 ? QStringLiteral("tile:%1").arg(tiles(frame_).value(piece_)) : QString();
}

void SmbPlayerFramesModel::setCurrentLibraryItem(const QString& id) {
    // Clicking a pattern puts it into the selected piece.
    if (piece_ < 0 || !id.startsWith(QLatin1String("tile:"))) {
        return;
    }
    const int tile = id.mid(5).toInt();
    if (tile != tiles(frame_).value(piece_)) {
        QString error;
        setTile(piece_, tile, &error);
    }
}

bool SmbPlayerFramesModel::setTile(int piece, int tile, QString* error) {
    if (piece < 0 || piece >= kTilesPerFrame || tile < 0 || tile > 255) {
        if (error) *error = QStringLiteral("A player pattern is 0-255.");
        return false;
    }
    const QString id = session_->playerFrames();
    const int frame = frame_;
    const QStringList names = frameNames(session_->document(), id);
    return session_->edit(QStringLiteral("Set a pattern of %1").arg(frameLabel(names.value(frame))),
                          [id, frame, piece, tile](SmbResourceDocument& d) {
                              QJsonObject r = d.resource(id);
                              QJsonArray framesJson = r.value(QStringLiteral("frames")).toArray();
                              QJsonArray tilesJson = framesJson.at(frame).toArray();
                              tilesJson.replace(piece, tile);
                              framesJson.replace(frame, tilesJson);
                              r.insert(QStringLiteral("frames"), framesJson);
                              d.setResource(id, r);
                          },
                          error);
}

bool SmbPlayerFramesModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("tile") && piece_ >= 0) {
        return setTile(piece_, value.toInt(), error);
    }
    if (key == QLatin1String("blank") && piece_ >= 0) {
        if (value.toString() == QLatin1String("Yes")) {
            return setTile(piece_, session_->document().playerBlankTile(session_->playerFrames()), error);
        }
        return true;
    }
    return CompositionModel::setField(key, value, error);
}

sdk::Inspection SmbPlayerFramesModel::inspect() const {
    sdk::Inspection inspection;
    const QStringList names = frameNames(session_->document(), session_->playerFrames());
    const QVector<int> t = tiles(frame_);
    const int blank = session_->document().playerBlankTile(session_->playerFrames());
    inspection.engineTag = QStringLiteral("smb.player-frames.v1");

    // The actions that show this frame.
    QStringList usedBy;
    const QVector<int> offsets =
        intsOf(session_->document().resource(session_->playerFrames()).value(QStringLiteral("frame_offsets")));
    for (int a = 0; a < offsets.size(); ++a) {
        if (offsets.at(a) / kTilesPerFrame == frame_) usedBy << actionNames().value(a);
    }

    if (piece_ < 0 || piece_ >= t.size()) {
        inspection.kicker = QStringLiteral("Player frame");
        inspection.title = frameLabel(names.value(frame_));
        sdk::Field frame;
        frame.key = QStringLiteral("frame");
        frame.label = QStringLiteral("Frame");
        frame.value = QStringLiteral("%1 of %2").arg(frame_).arg(names.size());
        frame.monospace = true;
        sdk::Field actions;
        actions.key = QStringLiteral("actions");
        actions.label = QStringLiteral("Shown for");
        actions.value = usedBy.isEmpty() ? QStringLiteral("animation steps") : usedBy.join(QStringLiteral(", "));
        sdk::Field patterns;
        patterns.key = QStringLiteral("patterns");
        patterns.label = QStringLiteral("Patterns");
        QStringList list;
        for (int v : t) list << (v == blank ? QStringLiteral("--") : hex(v).mid(1));
        patterns.value = list.join(QLatin1Char(' '));
        patterns.monospace = true;
        inspection.fields = {frame, actions, patterns};
        inspection.note = QStringLiteral("Each frame is eight 8x8 sprite patterns, two across and four down; "
                                         "%1 leaves a cell empty. Select a cell to change its pattern. Walk, swim "
                                         "and climb frames follow each other when the game animates them.")
                              .arg(hex(blank));
        return inspection;
    }

    inspection.kicker = QStringLiteral("Frame pattern");
    inspection.title = QStringLiteral("%1 · cell %2").arg(frameLabel(names.value(frame_))).arg(piece_ + 1);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = QStringLiteral("Pattern");
    tile.type = sdk::Field::Type::Integer;
    tile.value = t.at(piece_);
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = hex(t.at(piece_));
    tile.monospace = true;
    sdk::Field empty;
    empty.key = QStringLiteral("blank");
    empty.label = QStringLiteral("Empty");
    empty.type = sdk::Field::Type::Choice;
    empty.choices = QStringList{QStringLiteral("No"), QStringLiteral("Yes")};
    empty.value = t.at(piece_) == blank ? QStringLiteral("Yes") : QStringLiteral("No");
    inspection.fields = {tile, empty};
    inspection.note = QStringLiteral("Click a pattern in the library to put it here, or type its number. Patterns "
                                     "are shared: other frames that use one change with it in Tiles.");
    // Sprite patterns in the chosen player colours.
    const QVector<QRgb> pens = colors();
    const int base = session_->document().playerTileBase(session_->playerFrames());
    for (int p = 0; p < 256; ++p) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(p);
        item.label = hex(p);
        QImage icon(8, 8, QImage::Format_ARGB32);
        icon.fill(Qt::transparent);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int pixel = base + p < session_->document().tileCount(session_->chr())
                                      ? session_->document().pixel(session_->chr(), base + p, x, y)
                                      : 0;
                if (pixel) icon.setPixel(x, y, pens.value(pixel));
            }
        }
        item.icon = icon;
        inspection.library.items << item;
    }
    inspection.library.hint = QStringLiteral("click to put into the cell");
    return inspection;
}

QString SmbPlayerFramesModel::statusText() const {
    const QStringList names = frameNames(session_->document(), session_->playerFrames());
    return QStringLiteral("%1 · frame %2").arg(frameLabel(names.value(frame_))).arg(frame_);
}

// ---------------------------------------------------------------- actions

SmbPlayerActionsModel::SmbPlayerActionsModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SmbPlayerActionsModel::columns() const {
    sdk::TableColumn action;
    action.key = QStringLiteral("action");
    action.label = QStringLiteral("Action");
    action.width = 220;
    action.muted = true;
    sdk::TableColumn frame;
    frame.key = QStringLiteral("frame");
    frame.label = QStringLiteral("Shows");
    frame.editable = true;
    sdk::TableColumn byte;
    byte.key = QStringLiteral("byte");
    byte.label = QStringLiteral("Byte");
    byte.width = 90;
    byte.muted = true;
    byte.monospace = true;
    return {action, frame, byte};
}

int SmbPlayerActionsModel::rowCount() const {
    return intsOf(session_->document().resource(session_->playerFrames()).value(QStringLiteral("frame_offsets"))).size() + 2;
}

QString SmbPlayerActionsModel::cell(int row, int column) const {
    const QJsonObject r = session_->document().resource(session_->playerFrames());
    const QVector<int> offsets = intsOf(r.value(QStringLiteral("frame_offsets")));
    const QVector<int> kick = intsOf(r.value(QStringLiteral("swim_kick_tiles")));
    const QStringList names = frameNames(session_->document(), session_->playerFrames());
    if (row < offsets.size()) {
        if (column == 0) return actionNames().value(row, QStringLiteral("Action %1").arg(row));
        if (column == 1) return frameLabel(names.value(offsets.at(row) / kTilesPerFrame));
        if (column == 2) return hex(offsets.at(row));
        return {};
    }
    const int k = row - offsets.size();
    if (column == 0) return k == 0 ? QStringLiteral("Swim kick · left pattern") : QStringLiteral("Swim kick · right pattern");
    if (column == 1 || column == 2) return hex(kick.value(k));
    return {};
}

bool SmbPlayerActionsModel::isRowDirty(int row) const {
    return cell(row, 2) != [&] {
        const QJsonObject r = session_->saved().resource(session_->playerFrames());
        const QVector<int> offsets = intsOf(r.value(QStringLiteral("frame_offsets")));
        const QVector<int> kick = intsOf(r.value(QStringLiteral("swim_kick_tiles")));
        return row < offsets.size() ? hex(offsets.at(row)) : hex(kick.value(row - offsets.size()));
    }();
}

void SmbPlayerActionsModel::setCurrentRow(int row) {
    if (row != row_ && row >= 0 && row < rowCount()) {
        row_ = row;
        emit selectionChanged();
    }
}

bool SmbPlayerActionsModel::setValue(int row, const QString& text, QString* error) {
    const QString id = session_->playerFrames();
    const QJsonObject r = session_->document().resource(id);
    const int offsetCount = intsOf(r.value(QStringLiteral("frame_offsets"))).size();
    if (row < offsetCount) {
        // A frame by its name ("Big walk 1") or number.
        const QStringList names = frameNames(session_->document(), id);
        int frame = -1;
        for (int i = 0; i < names.size(); ++i) {
            if (frameLabel(names.at(i)).compare(text.trimmed(), Qt::CaseInsensitive) == 0) frame = i;
        }
        bool numeric = false;
        const int number = text.trimmed().toInt(&numeric);
        if (frame < 0 && numeric) frame = number;
        if (frame < 0 || frame >= names.size()) {
            if (error) *error = QStringLiteral("Name a frame, like Big walk 1, or give its number 0-%1.").arg(names.size() - 1);
            return false;
        }
        return session_->edit(QStringLiteral("%1 shows %2").arg(actionNames().value(row), frameLabel(names.at(frame))),
                              [id, row, frame](SmbResourceDocument& d) {
                                  QJsonObject res = d.resource(id);
                                  QJsonArray offsets = res.value(QStringLiteral("frame_offsets")).toArray();
                                  offsets.replace(row, frame * kTilesPerFrame);
                                  res.insert(QStringLiteral("frame_offsets"), offsets);
                                  d.setResource(id, res);
                              },
                              error);
    }
    const int k = row - offsetCount;
    QString t = text.trimmed();
    bool ok = false;
    const int tile = t.startsWith(QLatin1Char('$')) ? t.mid(1).toInt(&ok, 16) : t.toInt(&ok);
    if (!ok || tile < 0 || tile > 255) {
        if (error) *error = QStringLiteral("Give a pattern 0-255 (or $00-$FF).");
        return false;
    }
    return session_->edit(QStringLiteral("Swim kick pattern"),
                          [id, k, tile](SmbResourceDocument& d) {
                              QJsonObject res = d.resource(id);
                              QJsonArray kick = res.value(QStringLiteral("swim_kick_tiles")).toArray();
                              kick.replace(k, tile);
                              res.insert(QStringLiteral("swim_kick_tiles"), kick);
                              d.setResource(id, res);
                          },
                          error);
}

bool SmbPlayerActionsModel::setCell(int row, int column, const QString& value, QString* error) {
    if (column == 1) {
        return setValue(row, value, error);
    }
    return TableModel::setCell(row, column, value, error);
}

bool SmbPlayerActionsModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("frame") || key == QLatin1String("pattern")) {
        return setValue(row_, value.toString(), error);
    }
    return TableModel::setField(key, value, error);
}

sdk::Inspection SmbPlayerActionsModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject r = session_->document().resource(session_->playerFrames());
    const int offsetCount = intsOf(r.value(QStringLiteral("frame_offsets"))).size();
    inspection.engineTag = QStringLiteral("smb.player-frames.v1");
    inspection.title = cell(row_, 0);
    if (row_ < offsetCount) {
        inspection.kicker = QStringLiteral("Player action");
        sdk::Field frame;
        frame.key = QStringLiteral("frame");
        frame.label = QStringLiteral("Shows");
        frame.type = sdk::Field::Type::Choice;
        for (const QString& name : frameNames(session_->document(), session_->playerFrames())) {
            frame.choices << frameLabel(name);
        }
        frame.value = cell(row_, 1);
        inspection.fields = {frame};
        inspection.note = QStringLiteral("The first frame the game shows for this action; walking, swimming and "
                                         "climbing step on through the frames after it.");
    } else {
        inspection.kicker = QStringLiteral("Swim kick");
        sdk::Field pattern;
        pattern.key = QStringLiteral("pattern");
        pattern.label = QStringLiteral("Pattern");
        pattern.type = sdk::Field::Type::Text;
        pattern.value = cell(row_, 1);
        pattern.monospace = true;
        inspection.fields = {pattern};
        inspection.note = QStringLiteral("While swimming the game swaps these two patterns into the bottom row for "
                                         "the kick.");
    }
    inspection.engine = {{QStringLiteral("Byte"), cell(row_, 2)}};
    return inspection;
}

QString SmbPlayerActionsModel::statusText() const {
    return QStringLiteral("%1 · %2").arg(cell(row_, 0), cell(row_, 1));
}

}  // namespace rl::smb
