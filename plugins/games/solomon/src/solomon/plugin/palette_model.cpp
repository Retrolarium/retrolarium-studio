#include "solomon/plugin/palette_model.hpp"

#include "nes/editors.hpp"
#include "solomon/plugin/solomon_session.hpp"

#include <QJsonArray>

#include <algorithm>

namespace rl::solomon {

namespace {

constexpr int kGroupColourEntries[] = {1, 5, 9, 13};
constexpr int kPrincessEntry = 10;      // BG 2 colour 2, $16 in the princess rooms

}  // namespace

SolomonRoomPaletteModel::SolomonRoomPaletteModel(SolomonSession* session, int group)
    : PaletteModel(session), session_(session), group_(std::clamp(group, 0, kGroups - 1)) {
    connect(session_, &sdk::Session::contentChanged, this, &sdk::EditorModel::contentChanged);
}

QString SolomonRoomPaletteModel::groupLabel(int group) {
    if (group < 12) return tr("World %1").arg(group + 1);
    return group == 12 ? tr("Rooms 49, 50, 52") : tr("Room 53");
}

QString SolomonRoomPaletteModel::title() const {
    return tr("%1 palettes").arg(groupLabel(group_));
}

bool SolomonRoomPaletteModel::special() const {
    return groupColor(session_->state().art, group_) >= 0x80;
}

SolomonRoomPaletteModel::Source SolomonRoomPaletteModel::source(int flat) const {
    const bool groupEntry = std::find(std::begin(kGroupColourEntries), std::end(kGroupColourEntries), flat) !=
                            std::end(kGroupColourEntries);
    if (groupEntry) return special() ? Source::Fixed : Source::Group;
    if (flat == kPrincessEntry && special()) return Source::Fixed;
    return Source::Template;
}

QVector<sdk::PaletteRow> SolomonRoomPaletteModel::rows() const {
    const QVector<int> values = session_->painter().palette(group_);
    QVector<sdk::PaletteRow> rows;
    for (int r = 0; r < 8; ++r) {
        sdk::PaletteRow row;
        row.label = r < 4 ? tr("BG %1").arg(r) : tr("SPR %1").arg(r - 4);
        for (int i = r * 4; i < r * 4 + 4; ++i) {
            row.colors << nes::color(values.value(i));
            // The group's own entries carry a mark.
            row.labels << (source(i) == Source::Template ? nes::hex(values.value(i))
                                                         : QStringLiteral("%1*").arg(nes::hex(values.value(i))));
        }
        rows << row;
    }
    return rows;
}

void SolomonRoomPaletteModel::setCurrentEntry(QPoint entry) {
    if (entry != entry_) {
        entry_ = entry;
        emit selectionChanged();
    }
}

QVector<QColor> SolomonRoomPaletteModel::gamut() const {
    QVector<QColor> colors;
    for (int i = 0; i < 64; ++i) colors << nes::color(i);
    return colors;
}

QStringList SolomonRoomPaletteModel::gamutLabels() const {
    QStringList labels;
    for (int i = 0; i < 64; ++i) labels << nes::hex(i);
    return labels;
}

int SolomonRoomPaletteModel::currentGamutIndex() const {
    return session_->painter().palette(group_).value(flatEntry(), -1);
}

bool SolomonRoomPaletteModel::assignGamut(int index, QString* error) {
    const int flat = flatEntry();
    switch (source(flat)) {
    case Source::Fixed:
        if (error) {
            *error = tr("Rooms 49-56 take colour 1 of the background rows ($00) and BG 2 colour 2 ($16) "
                        "from the game's code, not from data.");
        }
        return false;
    case Source::Group: {
        const int group = group_;
        return session_->edit(tr("Set the %1 colour").arg(groupLabel(group_)),
                              [group, index](SolomonState& s) {
                                  QJsonArray colors = s.art.value(QStringLiteral("room_group_colors")).toArray();
                                  QJsonValue value = colors.at(group);
                                  if (value.isObject()) {
                                      QJsonObject o = value.toObject();
                                      o.insert(QStringLiteral("value"), index);
                                      value = o;
                                  } else {
                                      value = index;
                                  }
                                  colors.replace(group, value);
                                  s.art.insert(QStringLiteral("room_group_colors"), colors);
                              },
                              error);
    }
    case Source::Template:
        return session_->editBundle(tr("Set room palette colour %1").arg(flat),
                                    [flat, index](nes::Bundle& b) {
                                        b.setTableValue(QStringLiteral("room-palettes"), flat, index);
                                    },
                                    error);
    }
    return false;
}

QVector<sdk::PaletteSample> SolomonRoomPaletteModel::samples() const {
    QVector<sdk::PaletteSample> result;
    for (int room = 0; room < session_->rooms().roomCount(); ++room) {
        if (SolomonPainter::groupOf(room) != group_) continue;
        sdk::PaletteSample sample;
        sample.label = roomLabel(room + 1);
        sample.caption = tr("%1 as the game draws it").arg(roomLabel(room + 1));
        sample.image = session_->painter().room(room);
        result << sample;
    }
    return result;
}

QString SolomonRoomPaletteModel::statusText() const {
    return tr("%1 · %2 %3, colour %4")
        .arg(title(), entry_.y() < 4 ? QStringLiteral("BG") : QStringLiteral("SPR"))
        .arg(entry_.y() % 4)
        .arg(entry_.x());
}

sdk::Inspection SolomonRoomPaletteModel::inspect() const {
    sdk::Inspection inspection;
    inspection.kicker = tr("Palette entry");
    inspection.title = tr("%1 %2 · colour %3")
                           .arg(entry_.y() < 4 ? QStringLiteral("BG") : QStringLiteral("SPR"))
                           .arg(entry_.y() % 4)
                           .arg(entry_.x());
    const int value = currentGamutIndex();
    sdk::Field field;
    field.key = QStringLiteral("value");
    field.label = tr("Colour");
    field.value = value < 0 ? QString() : nes::hex(value);
    field.monospace = true;
    sdk::Field from;
    from.key = QStringLiteral("source");
    from.label = tr("Comes from");
    const Source s = source(flatEntry());
    from.value = s == Source::Group ? tr("this group") : s == Source::Fixed ? QStringLiteral("code")
                                                                                      : tr("all rooms");
    inspection.fields = {field, from};
    inspection.note = tr("Every four rooms load the same palettes with their own colour in entry 1 of "
                         "the four background rows (marked *). Changing any other entry changes it in "
                         "every room.");
    inspection.engineTag = QStringLiteral("solomon.room-art@1");
    inspection.engine = {{tr("Group"), QString::number(group_)},
                         {tr("Group colour"), nes::hex(groupColor(session_->state().art, group_))}};
    return inspection;
}

}  // namespace rl::solomon
