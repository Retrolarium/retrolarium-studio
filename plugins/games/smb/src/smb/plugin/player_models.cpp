#include "smb/plugin/player_models.hpp"

#include "sdk/i18n.hpp"
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
        rlTr("smb", "Big · jump"),   rlTr("smb", "Big · swim"),   rlTr("smb", "Big · stand"),
        rlTr("smb", "Big · skid"),   rlTr("smb", "Big · walk"),   rlTr("smb", "Big · climb"),
        rlTr("smb", "Big · crouch"), rlTr("smb", "Big · throw"),  rlTr("smb", "Small · jump"),
        rlTr("smb", "Small · swim"), rlTr("smb", "Small · stand"), rlTr("smb", "Small · skid"),
        rlTr("smb", "Small · walk"), rlTr("smb", "Small · climb"), rlTr("smb", "Small · killed"),
        rlTr("smb", "Small · throw slot")};
    return names;
}

}  // namespace

// ---------------------------------------------------------------- frames

namespace {

QString animationOf(const QString& frameName) {
    const int underscore = frameName.lastIndexOf(QLatin1Char('_'));
    bool numeric = false;
    if (underscore > 0) {
        frameName.mid(underscore + 1).toInt(&numeric);
    }
    return numeric ? frameName.left(underscore) : frameName;
}

}  // namespace

QStringList SmbPlayerFramesModel::animations(const QStringList& frameNames) {
    QStringList result;
    for (const QString& name : frameNames) {
        const QString key = animationOf(name);
        if (!result.contains(key)) result << key;
    }
    return result;
}

QVector<int> SmbPlayerFramesModel::framesOf(const QStringList& frameNames, const QString& animation) {
    QVector<int> result;
    for (int i = 0; i < frameNames.size(); ++i) {
        if (animationOf(frameNames.at(i)) == animation) result << i;
    }
    return result;
}

QString SmbPlayerFramesModel::animationLabel(const QString& animation) {
    return frameLabel(animation);
}

bool SmbPlayerFramesModel::mirrored(int frame, int cell) {
    // ChkForPlayerAttrib: graphics offsets $50 (big crouch), $B8 (small
    // stand), $C0 (growing) and $C8 (big stand) flip the right sprite of the
    // fourth row; while dying (small killed, $B0) the third row's too.
    switch (frame) {
    case 10: case 23: case 24: case 25: return cell == 7;
    case 22: return cell == 5 || cell == 7;
    default: return false;
    }
}

SmbPlayerFramesModel::SmbPlayerFramesModel(SmbSession* session, const QString& animation)
    : CompositionModel(session), session_(session), animation_(animation) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<int> SmbPlayerFramesModel::frameList() const {
    return framesOf(frameNames(session_->document(), session_->playerFrames()), animation_);
}

int SmbPlayerFramesModel::global() const {
    return frameList().value(frame_, 0);
}

QString SmbPlayerFramesModel::title() const {
    return animationLabel(animation_);
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

QImage SmbPlayerFramesModel::frameImage(const SmbSession* session, int frame, int palette) {
    QImage image(16, 32, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const SmbResourceDocument& d = session->document();
    const QVector<int> t = d.playerFrameTiles(session->playerFrames(), frame);
    const int blank = d.playerBlankTile(session->playerFrames());
    const int base = d.playerTileBase(session->playerFrames());
    const QVector<int> values = d.tableValues(session->playerPalette());
    QVector<QRgb> pens;
    for (int i = 0; i < 4; ++i) {
        pens << session->painter().nesColor(values.value(palette * 4 + i));
    }
    for (int i = 0; i < t.size() && i < kTilesPerFrame; ++i) {
        if (t.at(i) == blank) {
            continue;
        }
        const QPoint at((i & 1) * 8, (i / 2) * 8);
        const bool flip = mirrored(frame, i);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int pixel = d.pixel(session->chr(), base + t.at(i), flip ? 7 - x : x, y);
                if (pixel != 0) {
                    image.setPixel(at.x() + x, at.y() + y, pens.value(pixel));
                }
            }
        }
    }
    return image;
}

QImage SmbPlayerFramesModel::render() const {
    return frameImage(session_, global(), palette_);
}

QVector<sdk::ViewOption> SmbPlayerFramesModel::viewOptions() const {
    sdk::ViewOption palette;
    palette.key = QStringLiteral("palette");
    palette.label = tr("Colours");
    const QStringList names{tr("Hero"), tr("Brother"), tr("Fire")};
    palette.value = names.value(palette_);
    const int rows = session_->document().tableValues(session_->playerPalette()).size() / 4;
    palette.choices = names.mid(0, std::max(1, std::min(rows, int(names.size()))));
    return {palette};
}

void SmbPlayerFramesModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("palette")) {
        palette_ = std::max(0, QStringList{tr("Hero"), tr("Brother"), tr("Fire")}.indexOf(value));
        emit contentChanged();
        emit selectionChanged();
    }
}

QVector<sdk::CompositionPiece> SmbPlayerFramesModel::pieces() const {
    QVector<sdk::CompositionPiece> result;
    const QVector<int> t = tiles(global());
    for (int i = 0; i < t.size() && i < kTilesPerFrame; ++i) {
        sdk::CompositionPiece piece;
        piece.id = QStringLiteral("tile:%1").arg(i);
        piece.label = hex(t.at(i)) + (mirrored(global(), i) ? QStringLiteral(" ↔") : QString());
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
    for (int i : frameList()) {
        sdk::CompositionFrame frame;
        frame.id = names.at(i);
        frame.label = frameLabel(names.at(i));
        frame.detail = tr("frame %1 · %2").arg(i).arg(hex(i * kTilesPerFrame));
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
    return piece_ >= 0 ? QStringLiteral("tile:%1").arg(tiles(global()).value(piece_)) : QString();
}

void SmbPlayerFramesModel::setCurrentLibraryItem(const QString& id) {
    // Clicking a pattern puts it into the selected piece.
    if (piece_ < 0 || !id.startsWith(QLatin1String("tile:"))) {
        return;
    }
    const int tile = id.mid(5).toInt();
    if (tile != tiles(global()).value(piece_)) {
        QString error;
        setTile(piece_, tile, &error);
    }
}

bool SmbPlayerFramesModel::setTile(int piece, int tile, QString* error) {
    if (piece < 0 || piece >= kTilesPerFrame || tile < 0 || tile > 255) {
        if (error) *error = tr("A player pattern is 0-255.");
        return false;
    }
    const QString id = session_->playerFrames();
    const int frame = global();
    const QStringList names = frameNames(session_->document(), id);
    return session_->edit(tr("Set a pattern of %1").arg(frameLabel(names.value(frame))),
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
    const int frameIndex = global();
    const QVector<int> t = tiles(frameIndex);
    const int blank = session_->document().playerBlankTile(session_->playerFrames());
    inspection.engineTag = QStringLiteral("smb.player-frames.v1");

    // The actions that show this frame.
    QStringList usedBy;
    const QVector<int> offsets =
        intsOf(session_->document().resource(session_->playerFrames()).value(QStringLiteral("frame_offsets")));
    for (int a = 0; a < offsets.size(); ++a) {
        if (offsets.at(a) / kTilesPerFrame == frameIndex) usedBy << actionNames().value(a);
    }

    if (piece_ < 0 || piece_ >= t.size()) {
        inspection.kicker = tr("Player frame");
        inspection.title = frameLabel(names.value(frameIndex));
        sdk::Field frame;
        frame.key = QStringLiteral("frame");
        frame.label = tr("Frame");
        frame.value = tr("%1 of %2 · table %3").arg(frame_ + 1).arg(frameList().size()).arg(frameIndex);
        frame.monospace = true;
        sdk::Field actions;
        actions.key = QStringLiteral("actions");
        actions.label = tr("Shown for");
        actions.value = usedBy.isEmpty() ? tr("animation steps") : usedBy.join(QStringLiteral(", "));
        sdk::Field patterns;
        patterns.key = QStringLiteral("patterns");
        patterns.label = tr("Patterns");
        QStringList list;
        for (int v : t) list << (v == blank ? QStringLiteral("--") : hex(v).mid(1));
        patterns.value = list.join(QLatin1Char(' '));
        patterns.monospace = true;
        inspection.fields = {frame, actions, patterns};
        inspection.note = tr("Each frame is eight 8x8 sprite patterns, two across and four down; "
                             "%1 leaves a cell empty. Drag a pattern from the sheet onto a cell, or "
                             "select a cell and click one. Cells marked ↔ are drawn flipped by the "
                             "engine; SMB stores no flips of its own.")
                              .arg(hex(blank));
        return inspection;
    }

    inspection.kicker = tr("Frame pattern");
    inspection.title = tr("%1 · cell %2").arg(frameLabel(names.value(frameIndex))).arg(piece_ + 1);
    sdk::Field tile;
    tile.key = QStringLiteral("tile");
    tile.label = tr("Pattern");
    tile.type = sdk::Field::Type::Integer;
    tile.value = t.at(piece_);
    tile.minimum = 0;
    tile.maximum = 255;
    tile.hint = hex(t.at(piece_));
    tile.monospace = true;
    sdk::Field empty;
    empty.key = QStringLiteral("blank");
    empty.label = tr("Empty");
    empty.type = sdk::Field::Type::Choice;
    empty.choices = QStringList{tr("No"), QStringLiteral("Yes")};
    empty.value = t.at(piece_) == blank ? QStringLiteral("Yes") : tr("No");
    inspection.fields = {tile, empty};
    inspection.note = tr("Click a pattern in the Patterns sheet to put it here, or type its number. Patterns "
                         "are shared: other frames that use one change with it in Tiles.");
    if (mirrored(frameIndex, piece_)) {
        sdk::Field flip;
        flip.key = QStringLiteral("flip");
        flip.label = tr("Drawn");
        flip.value = tr("mirrored by the engine");
        inspection.fields << flip;
        inspection.note += tr(" The engine flips this cell, so it usually holds the same pattern as "
                              "the cell to its left.");
    }
    return inspection;
}

sdk::Library SmbPlayerFramesModel::sheet() const {
    return library();
}

sdk::Library SmbPlayerFramesModel::library() const {
    // The sprite pattern table in the chosen player colours, as a sheet.
    sdk::Library library;
    library.grid = true;
    library.hint = piece_ >= 0 ? tr("click: into the cell · or drag onto a cell")
                               : tr("drag onto a cell");
    const QVector<QRgb> pens = colors();
    const int base = session_->document().playerTileBase(session_->playerFrames());
    const int blank = session_->document().playerBlankTile(session_->playerFrames());
    for (int p = 0; p < 256; ++p) {
        sdk::LibraryItem item;
        item.id = QStringLiteral("tile:%1").arg(p);
        item.label = hex(p);
        item.hint = p == blank ? tr("empty cell") : QString();
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
        library.items << item;
    }
    return library;
}

bool SmbPlayerFramesModel::moveFrame(int from, int to, QString* error) {
    // Frames of an animation play in table order; moving one swaps its
    // patterns with its neighbour's, so the actions keep their offsets.
    const QVector<int> list = frameList();
    if (from < 0 || from >= list.size() || to < 0 || to >= list.size() || from == to) {
        if (error && list.size() <= 1) {
            *error = tr("%1 has a single frame.").arg(title());
        }
        return false;
    }
    const QString id = session_->playerFrames();
    const int a = list.at(from);
    const int b = list.at(to);
    const bool ok = session_->edit(tr("Move a frame of %1").arg(title()),
                                   [id, a, b](SmbResourceDocument& d) {
                                       QJsonObject r = d.resource(id);
                                       QJsonArray frames = r.value(QStringLiteral("frames")).toArray();
                                       const QJsonValue first = frames.at(a);
                                       frames.replace(a, frames.at(b));
                                       frames.replace(b, first);
                                       r.insert(QStringLiteral("frames"), frames);
                                       d.setResource(id, r);
                                   },
                                   error);
    if (ok) {
        frame_ = to;
        emit contentChanged();
        emit selectionChanged();
    }
    return ok;
}

bool SmbPlayerFramesModel::dropItem(const QString& libraryItem, const QString& piece, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:")) || !piece.startsWith(QLatin1String("tile:"))) {
        return CompositionModel::dropItem(libraryItem, piece, error);
    }
    selectPiece(piece);
    return setTile(piece.mid(5).toInt(), libraryItem.mid(5).toInt(), error);
}

QString SmbPlayerFramesModel::statusText() const {
    const QStringList names = frameNames(session_->document(), session_->playerFrames());
    return tr("%1 · frame %2").arg(frameLabel(names.value(global()))).arg(global());
}

// ---------------------------------------------------------------- actions

SmbPlayerActionsModel::SmbPlayerActionsModel(SmbSession* session) : TableModel(session), session_(session) {
    connect(session_, &SmbSession::documentChanged, this, &sdk::EditorModel::contentChanged);
}

QVector<sdk::TableColumn> SmbPlayerActionsModel::columns() const {
    sdk::TableColumn action;
    action.key = QStringLiteral("action");
    action.label = tr("Action");
    action.width = 220;
    action.muted = true;
    sdk::TableColumn frame;
    frame.key = QStringLiteral("frame");
    frame.label = tr("Shows");
    frame.editable = true;
    sdk::TableColumn byte;
    byte.key = QStringLiteral("byte");
    byte.label = tr("Byte");
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
    if (column == 0) return k == 0 ? tr("Swim kick · left pattern") : tr("Swim kick · right pattern");
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
            if (error) *error = tr("Name a frame, like Big walk 1, or give its number 0-%1.").arg(names.size() - 1);
            return false;
        }
        return session_->edit(tr("%1 shows %2").arg(actionNames().value(row), frameLabel(names.at(frame))),
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
        if (error) *error = tr("Give a pattern 0-255 (or $00-$FF).");
        return false;
    }
    return session_->edit(tr("Swim kick pattern"),
                          [id, k, tile](SmbResourceDocument& d) {
                              QJsonObject res = d.resource(id);
                              QJsonArray kick = res.value(QStringLiteral("swim_kick_tiles")).toArray();
                              kick.replace(k, tile);
                              res.insert(QStringLiteral("swim_kick_tiles"), kick);
                              d.setResource(id, res);
                          },
                          error);
}

QStringList SmbPlayerActionsModel::cellChoices(int row, int column) const {
    const int offsetCount =
        intsOf(session_->document().resource(session_->playerFrames()).value(QStringLiteral("frame_offsets"))).size();
    if (column != 1 || row >= offsetCount) {
        return {};
    }
    QStringList labels;
    for (const QString& name : frameNames(session_->document(), session_->playerFrames())) {
        labels << frameLabel(name);
    }
    return labels;
}

QVector<QImage> SmbPlayerActionsModel::cellChoiceIcons(int row, int column) const {
    QVector<QImage> icons;
    const int count = cellChoices(row, column).size();
    for (int f = 0; f < count; ++f) {
        icons << SmbPlayerFramesModel::frameImage(session_, f, 0);
    }
    return icons;
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
        inspection.kicker = tr("Player action");
        sdk::Field frame;
        frame.key = QStringLiteral("frame");
        frame.label = tr("Shows");
        frame.type = sdk::Field::Type::Choice;
        for (const QString& name : frameNames(session_->document(), session_->playerFrames())) {
            frame.choices << frameLabel(name);
        }
        frame.value = cell(row_, 1);
        inspection.fields = {frame};
        inspection.note = tr("The first frame the game shows for this action; walking, swimming and "
                             "climbing step on through the frames after it.");
    } else {
        inspection.kicker = tr("Swim kick");
        sdk::Field pattern;
        pattern.key = QStringLiteral("pattern");
        pattern.label = tr("Pattern");
        pattern.type = sdk::Field::Type::Text;
        pattern.value = cell(row_, 1);
        pattern.monospace = true;
        inspection.fields = {pattern};
        inspection.note = tr("While swimming the game swaps these two patterns into the bottom row for "
                             "the kick.");
    }
    inspection.engine = {{tr("Byte"), cell(row_, 2)}};
    return inspection;
}

QString SmbPlayerActionsModel::statusText() const {
    return QStringLiteral("%1 · %2").arg(cell(row_, 0), cell(row_, 1));
}

}  // namespace rl::smb
