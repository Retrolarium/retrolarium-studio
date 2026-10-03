#include "solomon/plugin/animation_model.hpp"

#include "nes/editors.hpp"
#include "solomon/data/names.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QJsonArray>
#include <QPainter>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kMaximumActions = 64;

QString hex(int value, int digits = 2) {
    return nes::hex(value, digits);
}

QJsonArray framesOf(const QJsonObject& animations) {
    return animations.value(QStringLiteral("frames")).toArray();
}

int frameIndexAt(const QJsonObject& animations, int address) {
    const QJsonArray frames = framesOf(animations);
    for (int i = 0; i < frames.size(); ++i) {
        if (frames.at(i).toObject().value(QStringLiteral("cpu_address")).toInt() == address) return i;
    }
    return -1;
}

// The halves of a frame's flags byte: horizontal and vertical flip and the
// two palette bits, as the game unpacks them into two OAM attributes.
struct Half {
    bool hflip = false;
    bool vflip = false;
    int palette = 0;
};

Half unpack(int flags, int half) {
    Half h;
    if (half == 0) {
        h.hflip = flags & 0x10;
        h.vflip = flags & 0x20;
        h.palette = ((flags >> 7) & 1) | (((flags >> 6) & 1) << 1);
    } else {
        h.hflip = flags & 0x02;
        h.vflip = flags & 0x01;
        h.palette = ((flags >> 2) & 1) | (((flags >> 3) & 1) << 1);
    }
    return h;
}

int pack(int flags, int half, const Half& h) {
    if (half == 0) {
        flags &= ~0xf0;
        flags |= (h.hflip ? 0x10 : 0) | (h.vflip ? 0x20 : 0) | ((h.palette & 1) << 7) | (((h.palette >> 1) & 1) << 6);
    } else {
        flags &= ~0x0f;
        flags |= (h.hflip ? 0x02 : 0) | (h.vflip ? 0x01 : 0) | ((h.palette & 1) << 2) | (((h.palette >> 1) & 1) << 3);
    }
    return flags;
}

const QStringList& yesNo() {
    static const QStringList kChoices{QStringLiteral("no"), QStringLiteral("yes")};
    return kChoices;
}

}  // namespace

// ---------------------------------------------------------------- tables

QJsonObject SolomonAnimationModel::descriptor(const QJsonObject& animations, int group, int action) {
    const QJsonArray pointers = animations.value(QStringLiteral("group_pointers")).toArray();
    if (group < 0 || group >= pointers.size()) return {};
    const int address = pointers.at(group).toInt() + action * 4;
    for (const QJsonValue& value : animations.value(QStringLiteral("descriptors")).toArray()) {
        if (value.toObject().value(QStringLiteral("cpu_address")).toInt() == address) return value.toObject();
    }
    return {};
}

QVector<QPair<int, int>> SolomonAnimationModel::actions(const QJsonObject& animations) {
    QVector<QPair<int, int>> result;
    const QJsonArray pointers = animations.value(QStringLiteral("group_pointers")).toArray();
    QVector<int> seen;
    for (int group = 0; group < pointers.size(); ++group) {
        const int pointer = pointers.at(group).toInt();
        if (seen.contains(pointer)) continue;
        seen << pointer;
        for (int action = 0; action < kMaximumActions; ++action) {
            if (descriptor(animations, group, action).isEmpty()) break;
            result << qMakePair(group, action);
        }
    }
    return result;
}

QString SolomonAnimationModel::groupLabel(int group) {
    if (group == 0) return QStringLiteral("Wizard");
    if (group < 6) return QStringLiteral("Objects $%1").arg(group * 4, 2, 16, QLatin1Char('0')).toUpper();
    // An enemy family, without the direction and speed of its variants.
    return enemyName(group * 4).section(QLatin1String(" ("), 0, 0);
}

QString SolomonAnimationModel::actionLabel(int group, int action) {
    if (group == 0 && action == 0x16) return QStringLiteral("Standing, right");
    if (group == 0 && action == 0x17) return QStringLiteral("Standing, left");
    if (group >= 6 && action >= 0x18 && action <= 0x1b) return QStringLiteral("Action %1 (shown first)").arg(hex(action));
    return QStringLiteral("Action %1").arg(hex(action));
}

QVector<int> SolomonAnimationModel::cycle(const QJsonObject& animations, int group, int action, int variant) {
    QVector<int> result;
    const QJsonObject d = descriptor(animations, group, action);
    if (d.isEmpty()) return result;
    int pointer = d.value(QStringLiteral("data_pointer")).toInt();
    if (d.value(QStringLiteral("uses_variants")).toBool()) {
        for (const QJsonValue& value : animations.value(QStringLiteral("selectors")).toArray()) {
            const QJsonObject selector = value.toObject();
            if (selector.value(QStringLiteral("cpu_address")).toInt() == pointer) {
                pointer = selector.value(QStringLiteral("pointers")).toArray().at(variant & 3).toInt();
                break;
            }
        }
    }
    // The phase counts its high nibble down to zero and then restarts from
    // its low nibble: the cycle shows frames low, low-1 ... 0, three bytes
    // apart from the data pointer.
    const int low = d.value(QStringLiteral("initial_phase")).toInt() & 0x0f;
    for (int high = low; high >= 0; --high) {
        const int index = frameIndexAt(animations, pointer + 3 * high + ((low >> 3) & 1));
        if (index >= 0) result << index;
    }
    return result;
}

// ---------------------------------------------------------------- model

SolomonAnimationModel::SolomonAnimationModel(SolomonSession* session, int group, int action)
    : CompositionModel(session), session_(session), group_(group), action_(action) {
    connect(session_, &sdk::Session::contentChanged, this, [this] {
        frame_ = std::clamp(frame_, 0, std::max(0, int(frameList().size()) - 1));
        emit contentChanged();
    });
    // Enemies show in the tileset of the rooms they mostly live in; start
    // with Wizard's and the first tileset.
    bank_ = 0;
    const QJsonObject first = frameRecord();
    tile_ = first.value(QStringLiteral("left_tile")).toInt();
}

QString SolomonAnimationModel::title() const {
    return QStringLiteral("%1 · %2").arg(groupLabel(group_), actionLabel(group_, action_));
}

QVector<int> SolomonAnimationModel::frameList() const {
    return cycle(session_->state().animations, group_, action_, variant_);
}

QJsonObject SolomonAnimationModel::frameRecord() const {
    const QVector<int> list = frameList();
    if (frame_ < 0 || frame_ >= list.size()) return {};
    return framesOf(session_->state().animations).at(list.at(frame_)).toObject();
}

QImage SolomonAnimationModel::render() const {
    const QJsonObject record = frameRecord();
    if (record.isEmpty()) {
        QImage blank(16, 16, QImage::Format_ARGB32);
        blank.fill(Qt::transparent);
        return blank;
    }
    return session_->painter().frameImage(record, bank_);
}

QVector<sdk::CompositionPiece> SolomonAnimationModel::pieces() const {
    if (frameRecord().isEmpty()) return {};
    return {{QStringLiteral("0"), QStringLiteral("Left"), QRect(0, 0, 8, 16), false},
            {QStringLiteral("1"), QStringLiteral("Right"), QRect(8, 0, 8, 16), false}};
}

void SolomonAnimationModel::selectPiece(const QString& id) {
    const int piece = id.isEmpty() ? -1 : std::clamp(id.toInt(), 0, 1);
    if (piece != piece_) {
        piece_ = piece;
        if (piece_ >= 0) {
            tile_ = frameRecord().value(piece_ == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile")).toInt();
        }
        emit selectionChanged();
        emit toolsChanged();
    }
}

QVector<sdk::CompositionFrame> SolomonAnimationModel::frames() const {
    QVector<sdk::CompositionFrame> result;
    const QJsonArray frames = framesOf(session_->state().animations);
    const QVector<int> list = frameList();
    for (int i = 0; i < list.size(); ++i) {
        sdk::CompositionFrame f;
        f.id = QString::number(list.at(i));
        f.label = QStringLiteral("Frame %1").arg(i + 1);
        f.detail = hex(frames.at(list.at(i)).toObject().value(QStringLiteral("cpu_address")).toInt(), 4);
        result << f;
    }
    return result;
}

void SolomonAnimationModel::setCurrentFrame(int frame) {
    frame = std::clamp(frame, 0, std::max(0, int(frameList().size()) - 1));
    if (frame != frame_) {
        frame_ = frame;
        emit contentChanged();
        emit selectionChanged();
    }
}

int SolomonAnimationModel::frameDurationMs(int) const {
    // The phase advances once per delay+1 game frames.
    const int delay = descriptor(session_->state().animations, group_, action_).value(QStringLiteral("delay")).toInt();
    return std::max(16, (delay + 1) * 1000 / 60);
}

bool SolomonAnimationModel::moveFrame(int, int, QString* error) {
    if (error) {
        *error = QStringLiteral("The game steps through frames that lie one after another in ROM; their order "
                                "is where they are. Edit a frame's patterns instead.");
    }
    return false;
}

int SolomonAnimationModel::halfPalette(int half) const {
    return unpack(frameRecord().value(QStringLiteral("flags")).toInt(), half).palette;
}

bool SolomonAnimationModel::editFrame(const QString& text, const std::function<void(QJsonObject&)>& change,
                                      QString* error) {
    const QVector<int> list = frameList();
    if (frame_ < 0 || frame_ >= list.size()) return false;
    const int index = list.at(frame_);
    return session_->edit(text,
                          [index, change](SolomonState& s) {
                              QJsonArray frames = s.animations.value(QStringLiteral("frames")).toArray();
                              QJsonObject record = frames.at(index).toObject();
                              change(record);
                              frames.replace(index, record);
                              s.animations.insert(QStringLiteral("frames"), frames);
                          },
                          error);
}

bool SolomonAnimationModel::dropItem(const QString& libraryItem, const QString& piece, QString* error) {
    if (!libraryItem.startsWith(QLatin1String("tile:")) || piece.isEmpty()) {
        return CompositionModel::dropItem(libraryItem, piece, error);
    }
    const int tile = std::clamp(libraryItem.mid(5).toInt(), 0, 255);
    const int half = std::clamp(piece.toInt(), 0, 1);
    tile_ = tile;
    piece_ = half;
    const QString key = half == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile");
    return editFrame(QStringLiteral("Set a pattern of %1").arg(title()),
                     [key, tile](QJsonObject& record) { record.insert(key, tile); }, error);
}

QString SolomonAnimationModel::currentLibraryItem() const {
    return QStringLiteral("tile:%1").arg(tile_);
}

void SolomonAnimationModel::setCurrentLibraryItem(const QString& id) {
    if (!id.startsWith(QLatin1String("tile:"))) return;
    tile_ = std::clamp(id.mid(5).toInt(), 0, 255);
    emit selectionChanged();
    // With a half selected, picking a pattern puts it there.
    if (piece_ >= 0) {
        const int current = frameRecord().value(piece_ == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile")).toInt();
        if (current != tile_) dropItem(id, QString::number(piece_), nullptr);
    }
}

sdk::Library SolomonAnimationModel::sheet() const {
    const int palette = piece_ >= 0 ? halfPalette(piece_) : halfPalette(0);
    const QString key = QStringLiteral("%1|%2|%3").arg(session_->revision()).arg(bank_).arg(palette);
    if (key != sheetKey_) {
        sheet_.clear();
        sheetKey_ = key;
        for (int tile = 0; tile < 256; ++tile) {
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = QStringLiteral("Pattern pair %1").arg(hex(tile));
            // Square for the sheet: the 8x16 pair centred.
            QImage icon(16, 16, QImage::Format_ARGB32);
            icon.fill(Qt::transparent);
            QPainter p(&icon);
            p.drawImage(4, 0, session_->painter().spritePair(tile, bank_, palette));
            p.end();
            item.icon = icon;
            sheet_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.hint = QStringLiteral("click: pattern · drag onto a half");
    library.items = sheet_;
    return library;
}

QVector<sdk::ViewOption> SolomonAnimationModel::viewOptions() const {
    sdk::ViewOption bank;
    bank.key = QStringLiteral("bank");
    bank.label = QStringLiteral("Tileset");
    bank.value = QString::number(bank_);
    bank.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    QVector<sdk::ViewOption> options{bank};
    if (descriptor(session_->state().animations, group_, action_).value(QStringLiteral("uses_variants")).toBool()) {
        sdk::ViewOption variant;
        variant.key = QStringLiteral("variant");
        variant.label = QStringLiteral("Variant");
        variant.value = QString::number(variant_);
        variant.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
        options << variant;
    }
    return options;
}

void SolomonAnimationModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("bank")) {
        bank_ = std::clamp(value.toInt(), 0, 3);
    } else if (key == QLatin1String("variant")) {
        variant_ = std::clamp(value.toInt(), 0, 3);
        frame_ = 0;
    } else {
        return;
    }
    emit contentChanged();
    emit toolsChanged();
}

QString SolomonAnimationModel::statusText() const {
    return QStringLiteral("%1 · frame %2 of %3").arg(title()).arg(frame_ + 1).arg(frameList().size());
}

sdk::Inspection SolomonAnimationModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject d = descriptor(session_->state().animations, group_, action_);
    const QJsonObject record = frameRecord();
    if (piece_ >= 0 && !record.isEmpty()) {
        const Half h = unpack(record.value(QStringLiteral("flags")).toInt(), piece_);
        const int tile = record.value(piece_ == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile")).toInt();
        inspection.kicker = QStringLiteral("Sprite half");
        inspection.title = piece_ == 0 ? QStringLiteral("Left half") : QStringLiteral("Right half");
        sdk::Field pattern;
        pattern.key = QStringLiteral("tile");
        pattern.label = QStringLiteral("Pattern pair");
        pattern.type = sdk::Field::Type::Integer;
        pattern.value = tile;
        pattern.minimum = 0;
        pattern.maximum = 255;
        pattern.hint = hex(tile);
        sdk::Field hflip;
        hflip.key = QStringLiteral("hflip");
        hflip.label = QStringLiteral("Flip across");
        hflip.type = sdk::Field::Type::Choice;
        hflip.value = yesNo().at(h.hflip ? 1 : 0);
        hflip.choices = yesNo();
        sdk::Field vflip = hflip;
        vflip.key = QStringLiteral("vflip");
        vflip.label = QStringLiteral("Flip down");
        vflip.value = yesNo().at(h.vflip ? 1 : 0);
        sdk::Field palette;
        palette.key = QStringLiteral("palette");
        palette.label = QStringLiteral("Palette");
        palette.type = sdk::Field::Type::Choice;
        palette.value = QStringLiteral("SPR %1").arg(h.palette);
        palette.choices = QStringList{QStringLiteral("SPR 0"), QStringLiteral("SPR 1"), QStringLiteral("SPR 2"), QStringLiteral("SPR 3")};
        inspection.fields = {pattern, hflip, vflip, palette};
        inspection.note = QStringLiteral("8x16 sprites: an even pattern pair comes from the sprite table, an odd "
                                         "one from the background table. Pick a pair in the sheet to put it here.");
        inspection.engineTag = QStringLiteral("solomon.animations@1");
        inspection.engine = {{QStringLiteral("Frame"), hex(record.value(QStringLiteral("cpu_address")).toInt(), 4)},
                             {QStringLiteral("Flags"), hex(record.value(QStringLiteral("flags")).toInt())}};
        return inspection;
    }
    inspection.kicker = QStringLiteral("Animation");
    inspection.title = title();
    sdk::Field delay;
    delay.key = QStringLiteral("delay");
    delay.label = QStringLiteral("Delay");
    delay.type = sdk::Field::Type::Integer;
    delay.value = d.value(QStringLiteral("delay")).toInt();
    delay.minimum = 0;
    delay.maximum = 255;
    delay.hint = QStringLiteral("frames between steps");
    sdk::Field count;
    count.key = QStringLiteral("frames");
    count.label = QStringLiteral("Frames");
    count.value = QString::number(frameList().size());
    count.monospace = true;
    inspection.fields = {delay, count};
    inspection.note = QStringLiteral("Select a half of the sprite to change its pattern, flips and palette. Frames "
                                     "are shared: every animation that shows a frame changes with it.");
    inspection.engineTag = QStringLiteral("solomon.animations@1");
    inspection.engine = {{QStringLiteral("Descriptor"), hex(d.value(QStringLiteral("cpu_address")).toInt(), 4)},
                         {QStringLiteral("Initial phase"), hex(d.value(QStringLiteral("initial_phase")).toInt())}};
    return inspection;
}

bool SolomonAnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("delay")) {
        const QJsonObject d = descriptor(session_->state().animations, group_, action_);
        const int address = d.value(QStringLiteral("cpu_address")).toInt();
        const int delay = std::clamp(value.toInt(), 0, 255);
        return session_->edit(QStringLiteral("Change the speed of %1").arg(title()),
                              [address, delay](SolomonState& s) {
                                  QJsonArray list = s.animations.value(QStringLiteral("descriptors")).toArray();
                                  for (int i = 0; i < list.size(); ++i) {
                                      QJsonObject o = list.at(i).toObject();
                                      if (o.value(QStringLiteral("cpu_address")).toInt() == address) {
                                          o.insert(QStringLiteral("delay"), delay);
                                          list.replace(i, o);
                                      }
                                  }
                                  s.animations.insert(QStringLiteral("descriptors"), list);
                              },
                              error);
    }
    if (piece_ < 0) return CompositionModel::setField(key, value, error);
    const int half = piece_;
    if (key == QLatin1String("tile")) {
        return dropItem(QStringLiteral("tile:%1").arg(std::clamp(value.toInt(), 0, 255)), QString::number(half), error);
    }
    if (key == QLatin1String("hflip") || key == QLatin1String("vflip") || key == QLatin1String("palette")) {
        const QString text = value.toString();
        return editFrame(QStringLiteral("Edit a frame of %1").arg(title()),
                         [key, text, half](QJsonObject& record) {
                             const int flags = record.value(QStringLiteral("flags")).toInt();
                             Half h = unpack(flags, half);
                             if (key == QLatin1String("hflip")) h.hflip = text == QLatin1String("yes");
                             else if (key == QLatin1String("vflip")) h.vflip = text == QLatin1String("yes");
                             else h.palette = std::clamp(text.section(QLatin1Char(' '), 1).toInt(), 0, 3);
                             record.insert(QStringLiteral("flags"), pack(flags, half, h));
                         },
                         error);
    }
    return CompositionModel::setField(key, value, error);
}

}  // namespace rl::solomon
