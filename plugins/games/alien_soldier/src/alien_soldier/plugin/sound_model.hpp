#pragma once

#include "sdk/editor_model.hpp"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <optional>

namespace rl::alien_soldier {

class AlienSoldierSession;

// One song or sound effect on a piano roll. Each channel's notes come from
// following its sequence the way the 68000 driver reads it (durations, ties,
// loops, calls, transposition), once through: a song's closing jump back
// ends the roll. A note's pitch is its byte above $81 plus the channel's
// transposition; editing it rewrites that byte (every pass through it), and
// its length rewrites its own duration byte where it has one. The DAC
// channel's "notes" are sample numbers. Listen renders the payload with the
// kit's sequencer of the driver (unsaved edits included) and plays the WAV.
class SoundModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    SoundModel(AlienSoldierSession* session, const QString& label);

    QString title() const override { return label_; }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QString chipLabel() const override;
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;
    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override { return 0; }
    int pitchMaximum() const override { return 95; }
    QString pitchName(int pitch) const override;
    int ticksPerBar() const override { return 48; }

    QString selectedNote() const override { return note_; }
    void selectNote(const QString& id) override;
    void beginEdit(const QString& id) override;
    bool editNote(const QString& id, int pitch, int length) override;
    void endEdit() override;

    bool canPreview() const override { return true; }
    void preview() override;
    void stopPreview() override;
    bool previewLoops() const override;
    void setPreviewLoops(bool loop) override;

private:
    struct Event {
        int offset = -1;          // the note byte, or -1 for a lone duration
        int durationOffset = -1;  // its own duration byte, or -1
        int start = 0;
        int length = 1;
        int note = 0x80;  // $80 rest
        int transpose = 0;
    };
    QJsonObject payload() const;
    QByteArray bytes() const;
    QVector<Event> walk(int channel) const;
    // id "channel:offset:start" -> that event
    std::optional<Event> eventOf(const QString& id) const;

    AlienSoldierSession* session_;
    QString label_;
    int channel_ = 0;
    QString note_;
    QSet<int> muted_;
    mutable QHash<int, QVector<Event>> cache_;
    mutable quint64 cacheRevision_ = ~quint64(0);
    QJsonObject editBefore_;
    quint64 editRevision_ = 0;
};

// One DAC sample on its own: a voice the game asks for (requests $10-$3E:
// "Alien Soldier", "Let's go") or a drum the music's DAC channel plays
// (notes $81 on). Listen has the kit write its levels at the Z80's pace.
class SampleModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    // `document` is "voice:<id>" or "drum:<id>", the id in hexadecimal.
    SampleModel(AlienSoldierSession* session, const QString& document);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override { return title(); }

    QString chipLabel() const override;
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return 0; }
    void setCurrentChannel(int) override {}
    QVector<sdk::SequencerNote> notes(int) const override { return {}; }

    bool canPreview() const override { return descriptor().contains(QStringLiteral("bank")); }
    void preview() override;
    void stopPreview() override;
    bool previewLoops() const override;
    void setPreviewLoops(bool loop) override;

    // The groups' names for a sample ("Voice $10", "Drum $81").
    static QString sampleTitle(bool voice, int id);

private:
    QJsonObject descriptor() const;
    int sampleBytes() const;

    AlienSoldierSession* session_;
    bool voice_ = true;
    int id_ = 0;
};

}  // namespace rl::alien_soldier
