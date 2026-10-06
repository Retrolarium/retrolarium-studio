#include "solomon/plugin/animation_model.hpp"

#include "nes/editors.hpp"
#include "solomon/data/names.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>

namespace rl::solomon {

namespace {

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
    QSet<int> descriptors;
    int last = 0;
    for (const QJsonValue& value : animations.value(QStringLiteral("descriptors")).toArray()) {
        const int address = value.toObject().value(QStringLiteral("cpu_address")).toInt();
        descriptors.insert(address);
        last = std::max(last, address + 4);
    }
    QVector<int> starts;
    for (const QJsonValue& value : pointers) starts << value.toInt();
    std::sort(starts.begin(), starts.end());
    QVector<int> seen;
    for (int group = 0; group < pointers.size(); ++group) {
        const int pointer = pointers.at(group).toInt();
        if (seen.contains(pointer)) continue;
        seen << pointer;
        // A group's descriptors run up to the next group's; the variant
        // selectors sit in two of those gaps.
        const auto next = std::upper_bound(starts.begin(), starts.end(), pointer);
        const int end = next == starts.end() ? last : *next;
        for (int action = 0; pointer + action * 4 < end; ++action) {
            if (descriptors.contains(pointer + action * 4)) result << qMakePair(group, action);
        }
    }
    return result;
}

QJsonObject SolomonAnimationModel::mirrored(const QJsonObject& record, int transform) {
    const int flags = record.value(QStringLiteral("flags")).toInt();
    Half left = unpack(flags, 0);
    Half right = unpack(flags, 1);
    int leftTile = record.value(QStringLiteral("left_tile")).toInt();
    int rightTile = record.value(QStringLiteral("right_tile")).toInt();
    if (transform & 1) {
        std::swap(left, right);
        std::swap(leftTile, rightTile);
        left.hflip = !left.hflip;
        right.hflip = !right.hflip;
    }
    if (transform & 2) {
        left.vflip = !left.vflip;
        right.vflip = !right.vflip;
    }
    QJsonObject result = record;
    result.insert(QStringLiteral("left_tile"), leftTile);
    result.insert(QStringLiteral("right_tile"), rightTile);
    result.insert(QStringLiteral("flags"), pack(pack(flags, 0, left), 1, right));
    return result;
}

QVector<AnimationFamily> SolomonAnimationModel::families(const QJsonObject& animations) {
    struct Entry {
        int group;
        int action;
        int delay;
        bool variants;
        QVector<QVector<QJsonObject>> cycles;   // per variant
    };
    const QJsonArray frames = framesOf(animations);
    QVector<Entry> entries;
    for (const auto& action : actions(animations)) {
        const QJsonObject d = descriptor(animations, action.first, action.second);
        Entry e{action.first, action.second, d.value(QStringLiteral("delay")).toInt(),
                d.value(QStringLiteral("uses_variants")).toBool(), {}};
        for (int variant = 0; variant < (e.variants ? 4 : 1); ++variant) {
            QVector<QJsonObject> cycle;
            for (int index : SolomonAnimationModel::cycle(animations, action.first, action.second, variant)) {
                QJsonObject record = frames.at(index).toObject();
                record.remove(QStringLiteral("cpu_address"));
                cycle << record;
            }
            e.cycles << cycle;
        }
        entries << e;
    }
    QVector<AnimationFamily> result;
    QVector<bool> taken(entries.size(), false);
    for (int i = 0; i < entries.size(); ++i) {
        if (taken.at(i)) continue;
        taken[i] = true;
        const Entry& first = entries.at(i);
        AnimationFamily family;
        family.group = first.group;
        family.members << AnimationMember{first.action, 0};
        family.frames = first.cycles.value(0).size();
        for (int j = i + 1; j < entries.size(); ++j) {
            const Entry& other = entries.at(j);
            if (taken.at(j) || other.group != first.group || other.delay != first.delay ||
                other.variants != first.variants) {
                continue;
            }
            for (int transform = 0; transform < 4; ++transform) {
                bool same = true;
                for (int v = 0; v < first.cycles.size() && same; ++v) {
                    const QVector<QJsonObject>& a = first.cycles.at(v);
                    const QVector<QJsonObject>& b = other.cycles.at(v);
                    same = a.size() == b.size();
                    for (int f = 0; f < a.size() && same; ++f) same = mirrored(a.at(f), transform) == b.at(f);
                }
                if (same) {
                    family.members << AnimationMember{other.action, transform};
                    taken[j] = true;
                    break;
                }
            }
        }
        result << family;
    }
    return result;
}

QString SolomonAnimationModel::familyLabel(const AnimationFamily& family) {
    const AnimationMember& first = family.members.first();
    if (family.members.size() == 1) return actionLabel(family.group, first.action);
    QStringList names;
    for (int i = 0; i < family.members.size() && i < 3; ++i) names << hex(family.members.at(i).action);
    QString list = names.join(QStringLiteral(", "));
    if (family.members.size() > 3) list += QStringLiteral(" +%1").arg(family.members.size() - 3);
    const QString name = actionLabel(family.group, first.action);
    if (!name.startsWith(QLatin1String("Action "))) return QStringLiteral("%1 · %2").arg(name.section(QLatin1Char(','), 0, 0), list);
    return tr("Actions %1").arg(list);
}

QString SolomonAnimationModel::groupLabel(int group) {
    if (group == 0) return tr("Wizard");
    if (group < 6) return tr("Objects $%1").arg(group * 4, 2, 16, QLatin1Char('0')).toUpper();
    // An enemy family, without the direction and speed of its variants.
    return enemyName(group * 4).section(QLatin1String(" ("), 0, 0);
}

QString SolomonAnimationModel::actionLabel(int group, int action) {
    if (group == 0 && action == 0x16) return tr("Standing, right");
    if (group == 0 && action == 0x17) return tr("Standing, left");
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
    return QStringLiteral("%1 · %2").arg(groupLabel(group_), familyLabel(family()));
}

AnimationFamily SolomonAnimationModel::family() const {
    for (const AnimationFamily& f : session_->animationFamilies()) {
        if (f.group != group_) continue;
        for (const AnimationMember& m : f.members) {
            if (m.action == action_) return f;
        }
    }
    AnimationFamily alone;
    alone.group = group_;
    alone.members << AnimationMember{action_, 0};
    return alone;
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
    return {{QStringLiteral("0"), tr("Left"), QRect(0, 0, 8, 16), false},
            {QStringLiteral("1"), tr("Right"), QRect(8, 0, 8, 16), false}};
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
        f.label = tr("Frame %1").arg(i + 1);
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
        *error = tr("The game steps through frames that lie one after another in ROM; their order "
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
    // The same frame of every linked action, mirrored as that action shows
    // it; a record two actions would need differently keeps the first.
    QVector<QPair<int, int>> targets{{index, 0}};
    if (linked_) {
        const QJsonObject& animations = session_->state().animations;
        const AnimationFamily f = family();
        int own = 0;
        for (const AnimationMember& m : f.members) {
            if (m.action == action_) own = m.transform;
        }
        for (const AnimationMember& m : f.members) {
            const QVector<int> cycle = SolomonAnimationModel::cycle(animations, group_, m.action, variant_);
            if (frame_ < cycle.size()) targets << qMakePair(cycle.at(frame_), m.transform ^ own);
        }
    }
    return session_->edit(text,
                          [targets, change](SolomonState& s) {
                              QJsonArray frames = s.animations.value(QStringLiteral("frames")).toArray();
                              QJsonObject record = frames.at(targets.first().first).toObject();
                              change(record);
                              QHash<int, QJsonObject> written;
                              for (const auto& target : targets) {
                                  if (written.contains(target.first)) continue;
                                  QJsonObject next = mirrored(record, target.second);
                                  next.insert(QStringLiteral("cpu_address"),
                                              frames.at(target.first).toObject().value(QStringLiteral("cpu_address")));
                                  written.insert(target.first, next);
                                  frames.replace(target.first, next);
                              }
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
    return editFrame(tr("Set a pattern of %1").arg(title()),
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
        // Both pattern tables side by side as 8x16 pairs, the way a pattern
        // viewer shows them in 8x16 mode: even pairs come from the sprite
        // table, odd ones from the background table.
        for (int i = 0; i < 256; ++i) {
            const int row = i / 32;
            const int column = i % 32;
            const int tile = (row * 16 + column % 16) * 2 + column / 16;
            sdk::LibraryItem item;
            item.id = QStringLiteral("tile:%1").arg(tile);
            item.label = tr("Pattern pair %1").arg(hex(tile));
            item.hint = tile & 1 ? tr("background table") : tr("sprite table");
            item.icon = session_->painter().spritePair(tile, bank_, palette);
            sheet_ << item;
        }
    }
    sdk::Library library;
    library.grid = true;
    library.columns = 32;
    library.cell = QSize(8, 16);
    library.hint = tr("click: pattern · drag onto a half");
    library.items = sheet_;
    return library;
}

QVector<sdk::ViewOption> SolomonAnimationModel::viewOptions() const {
    sdk::ViewOption bank;
    bank.key = QStringLiteral("bank");
    bank.label = tr("Tileset");
    bank.value = QString::number(bank_);
    bank.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    QVector<sdk::ViewOption> options{bank};
    if (family().members.size() > 1) {
        sdk::ViewOption linked;
        linked.key = QStringLiteral("linked");
        linked.label = tr("Mirrors");
        linked.value = linked_ ? QStringLiteral("Linked") : tr("Separate");
        linked.choices = QStringList{QStringLiteral("Linked"), tr("Separate")};
        options << linked;
    }
    if (descriptor(session_->state().animations, group_, action_).value(QStringLiteral("uses_variants")).toBool()) {
        sdk::ViewOption variant;
        variant.key = QStringLiteral("variant");
        variant.label = tr("Variant");
        variant.value = QString::number(variant_);
        variant.choices = QStringList{QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
        options << variant;
    }
    return options;
}

void SolomonAnimationModel::setViewOption(const QString& key, const QString& value) {
    if (key == QLatin1String("bank")) {
        bank_ = std::clamp(value.toInt(), 0, 3);
    } else if (key == QLatin1String("linked")) {
        linked_ = value == QLatin1String("Linked");
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
    return tr("%1 · frame %2 of %3").arg(title()).arg(frame_ + 1).arg(frameList().size());
}

sdk::Inspection SolomonAnimationModel::inspect() const {
    sdk::Inspection inspection;
    const QJsonObject d = descriptor(session_->state().animations, group_, action_);
    const QJsonObject record = frameRecord();
    if (piece_ >= 0 && !record.isEmpty()) {
        const Half h = unpack(record.value(QStringLiteral("flags")).toInt(), piece_);
        const int tile = record.value(piece_ == 0 ? QStringLiteral("left_tile") : QStringLiteral("right_tile")).toInt();
        inspection.kicker = tr("Sprite half");
        inspection.title = piece_ == 0 ? tr("Left half") : tr("Right half");
        sdk::Field pattern;
        pattern.key = QStringLiteral("tile");
        pattern.label = tr("Pattern pair");
        pattern.type = sdk::Field::Type::Integer;
        pattern.value = tile;
        pattern.minimum = 0;
        pattern.maximum = 255;
        pattern.hint = hex(tile);
        sdk::Field hflip;
        hflip.key = QStringLiteral("hflip");
        hflip.label = tr("Flip across");
        hflip.type = sdk::Field::Type::Choice;
        hflip.value = yesNo().at(h.hflip ? 1 : 0);
        hflip.choices = yesNo();
        sdk::Field vflip = hflip;
        vflip.key = QStringLiteral("vflip");
        vflip.label = tr("Flip down");
        vflip.value = yesNo().at(h.vflip ? 1 : 0);
        sdk::Field palette;
        palette.key = QStringLiteral("palette");
        palette.label = tr("Palette");
        palette.type = sdk::Field::Type::Choice;
        palette.value = tr("SPR %1").arg(h.palette);
        palette.choices = QStringList{tr("SPR 0"), tr("SPR 1"), tr("SPR 2"), tr("SPR 3")};
        inspection.fields = {pattern, hflip, vflip, palette};
        inspection.note = tr("8x16 sprites: an even pattern pair comes from the sprite table, an odd "
                             "one from the background table. Pick a pair in the sheet to put it here.");
        inspection.engineTag = QStringLiteral("solomon.animations@1");
        inspection.engine = {{tr("Frame"), hex(record.value(QStringLiteral("cpu_address")).toInt(), 4)},
                             {tr("Flags"), hex(record.value(QStringLiteral("flags")).toInt())}};
        return inspection;
    }
    inspection.kicker = tr("Animation");
    inspection.title = title();
    sdk::Field delay;
    delay.key = QStringLiteral("delay");
    delay.label = tr("Delay");
    delay.type = sdk::Field::Type::Integer;
    delay.value = d.value(QStringLiteral("delay")).toInt();
    delay.minimum = 0;
    delay.maximum = 255;
    delay.hint = tr("frames between steps");
    sdk::Field count;
    count.key = QStringLiteral("frames");
    count.label = QStringLiteral("Frames");
    count.value = QString::number(frameList().size());
    count.monospace = true;
    inspection.fields = {delay, count};
    // Which actions show it, and how.
    static const char* const kHow[] = {"as drawn", "mirrored across", "mirrored down", "turned over"};
    const AnimationFamily f = family();
    for (int transform = 0; transform < 4; ++transform) {
        QStringList names;
        for (const AnimationMember& m : f.members) {
            if (m.transform == transform) names << hex(m.action);
        }
        if (names.isEmpty()) continue;
        sdk::Field shown;
        shown.key = QStringLiteral("shown_%1").arg(transform);
        shown.label = QString::fromLatin1(kHow[transform]);
        shown.value = names.join(QLatin1Char(' '));
        shown.monospace = true;
        inspection.fields << shown;
    }
    inspection.note = tr("The game keeps a frame record per facing: left and right are the same "
                         "patterns with the halves swapped and flipped. Actions that show the same "
                         "frames, as drawn or mirrored, are one entry; with Mirrors linked an edit "
                         "reaches all of them. Select a half to change its pattern, flips and palette.");
    inspection.engineTag = QStringLiteral("solomon.animations@1");
    inspection.engine = {{tr("Descriptor"), hex(d.value(QStringLiteral("cpu_address")).toInt(), 4)},
                         {tr("Initial phase"), hex(d.value(QStringLiteral("initial_phase")).toInt())}};
    return inspection;
}

bool SolomonAnimationModel::setField(const QString& key, const QVariant& value, QString* error) {
    if (key == QLatin1String("delay")) {
        const QJsonObject d = descriptor(session_->state().animations, group_, action_);
        const int address = d.value(QStringLiteral("cpu_address")).toInt();
        const int delay = std::clamp(value.toInt(), 0, 255);
        QSet<int> addresses{address};
        if (linked_) {
            for (const AnimationMember& m : family().members) {
                addresses.insert(descriptor(session_->state().animations, group_, m.action)
                                     .value(QStringLiteral("cpu_address"))
                                     .toInt());
            }
        }
        return session_->edit(tr("Change the speed of %1").arg(title()),
                              [addresses, delay](SolomonState& s) {
                                  QJsonArray list = s.animations.value(QStringLiteral("descriptors")).toArray();
                                  for (int i = 0; i < list.size(); ++i) {
                                      QJsonObject o = list.at(i).toObject();
                                      if (addresses.contains(o.value(QStringLiteral("cpu_address")).toInt())) {
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
        return editFrame(tr("Edit a frame of %1").arg(title()),
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
