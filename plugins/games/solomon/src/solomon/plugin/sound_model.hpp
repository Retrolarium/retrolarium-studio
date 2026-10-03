#pragma once

#include "sdk/editor_model.hpp"

#include <QHash>
#include <QJsonObject>
#include <QSet>

namespace rl::solomon {

class SolomonSession;

// One note a virtual channel plays: when, for how long, and which command
// of the shared stream storage played it.
struct SolomonNote {
    int start = 0;          // frames
    int length = 1;
    int command = -1;       // index into the document's commands
    int token = 0;          // the note byte
};

// One of the 26 audio programs (room music, sound effects) on the piano
// roll: its virtual channels as the engine steps them, notes by the APU
// voice they sound on. Dragging a note rewrites the byte of the command
// that plays it, so every pass through that command changes with it.
class SolomonSoundModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    SolomonSoundModel(SolomonSession* session, int program);

    static QString programLabel(int program);
    // {title, programs} in the order the tracker lists them.
    static QVector<QPair<QString, QVector<int>>> programGroups();
    // The notes each virtual channel of a program plays in its first
    // `frames` frames, keyed by the channel's number.
    static QHash<int, QVector<SolomonNote>> trace(const QJsonObject& audio, int program, int frames);

    QString title() const override;
    sdk::Inspection inspect() const override;
    QString statusText() const override;
    sdk::Budget budget() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · APU"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;

    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override { return 12; }
    int pitchMaximum() const override { return 12 + 12 * 8 + 11; }
    int ticksPerBar() const override { return 32; }

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
    QVector<int> numbers() const;           // virtual channels, in program order
    const QHash<int, QVector<SolomonNote>>& traced() const;
    bool find(const QString& id, int* number, int* index) const;
    int pitchOf(int number, int token) const;
    QString voiceOf(int number) const;

    SolomonSession* session_;
    int program_;
    int channel_ = 0;
    QString selected_;
    QSet<QString> mutedVoices_;
    // A drag edits this copy of the commands and writes once.
    bool editing_ = false;
    QJsonObject draft_;

    mutable QHash<int, QVector<SolomonNote>> trace_;
    mutable QJsonObject traceOf_;
};

}  // namespace rl::solomon
