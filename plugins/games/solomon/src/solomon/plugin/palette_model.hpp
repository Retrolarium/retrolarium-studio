#pragma once

#include "sdk/editor_model.hpp"

namespace rl::solomon {

class SolomonSession;

// The palettes a group of rooms loads (a world's four, and room 51 with
// world 1): the shared room template with the group's own colour in entry 1
// of the four background rows. Groups 12 and 13 (rooms 49-53) take the
// princess-room colours fixed in code instead. Editing entry 1 of a
// background row changes the group's colour; every other entry is the
// template every room shares.
class SolomonRoomPaletteModel final : public sdk::PaletteModel {
    Q_OBJECT

public:
    SolomonRoomPaletteModel(SolomonSession* session, int group);

    static constexpr int kGroups = 14;
    static QString groupLabel(int group);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::PaletteRow> rows() const override;
    QPoint currentEntry() const override { return entry_; }
    void setCurrentEntry(QPoint entry) override;
    Picker picker() const override { return Picker::Gamut; }
    QString pickerTitle() const override { return QStringLiteral("NES palette · 64 colours"); }
    QVector<QColor> gamut() const override;
    QStringList gamutLabels() const override;
    int currentGamutIndex() const override;
    bool assignGamut(int index, QString* error) override;
    QVector<sdk::PaletteSample> samples() const override;

private:
    int flatEntry() const { return entry_.y() * 4 + entry_.x(); }
    bool special() const;
    // Where an entry comes from: the group colour, a value fixed in code or
    // the shared template.
    enum class Source { Group, Fixed, Template };
    Source source(int flat) const;

    SolomonSession* session_;
    int group_;
    QPoint entry_;
};

}  // namespace rl::solomon
