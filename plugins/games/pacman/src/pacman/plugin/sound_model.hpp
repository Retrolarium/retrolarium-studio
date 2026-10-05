#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QVector>

namespace rl::pacman {

class PacmanSession;

// A tune or sound effect of pacman.sound-streams.v1 on the piano roll: one
// lane per slot playing together (the player-ready and intermission tunes
// take two square channels). Notes keep their order; drag a note to
// retune it or stretch it to change its duration in frames.
class PacmanSoundModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    PacmanSoundModel(PacmanSession* session, QVector<int> streams, QString title);

    QString title() const override { return title_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Budget budget() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · square"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;

    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override { return 45; }
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

    // A sound byte's MIDI pitch and back (-1: out of the engine's range).
    static int pitchOf(int value);
    static int valueOf(int pitch);
    // Bytes a stream takes in the arena.
    static int streamBytes(const QJsonObject& stream);

private:
    QJsonObject sound() const;              // the draft while dragging
    QJsonObject stream(int channel) const;
    bool commit(const QString& text, const QJsonObject& next, QString* error);
    QJsonObject withCommand(const QJsonObject& sound, int channel, int command, const QJsonObject& value) const;
    QJsonObject withPrologue(const QJsonObject& sound, int channel, int index, int value) const;

    PacmanSession* session_;
    QVector<int> slots_;
    QString title_;
    int channel_ = 0;
    QString selected_;      // "channel:command"
    bool editing_ = false;
    QJsonObject draft_;
};

}  // namespace rl::pacman
