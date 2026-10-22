#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QSet>

namespace rl::doraemon {

class DoraemonSession;

// One of the 26 tracks of Doraemon's four music drivers on the piano roll:
// pulse 1, pulse 2, triangle and noise as the driver plays them (traced by
// the kit through loops, calls and saved positions). Dragging a tonal note
// changes its event byte by semitones; every place the stream is played
// follows, since loops play the same bytes.
class DoraemonMusicModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    static constexpr int kFrames = 2400;  // 40 seconds traced and rendered
    static QString trackTitle(int driver, int track);

    DoraemonMusicModel(DoraemonSession* session, int driver, int track);

    QString title() const override { return trackTitle(driver_, track_); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Budget budget() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · APU"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;

    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override { return 24; }
    int pitchMaximum() const override { return 108; }
    int ticksPerBar() const override { return 48; }

    QString selectedNote() const override { return selected_; }
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
    // Where an event address lives in the music document.
    struct Location {
        int segment = -1;
        int event = -1;
        QString text;
    };
    Location locate(int address) const;
    QJsonObject trace() const;
    QJsonObject eventAt(const QString& noteId) const;  // the trace event of a note id
    bool shiftEvent(int address, int semitones, const QString& label, QString* error);

    DoraemonSession* session_;
    int driver_;
    int track_;
    int channel_ = 0;
    QString selected_;
    QSet<int> muted_;
    // While dragging: the note's semitone shift, applied on release.
    QString dragging_;
    int dragShift_ = 0;
    int dragBase_ = 0;
};

}  // namespace rl::doraemon
