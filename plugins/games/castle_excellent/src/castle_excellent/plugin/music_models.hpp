#pragma once

#include "castle_excellent/data/music.hpp"
#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QSet>

namespace rl::castle_excellent {

class CastleExcellentSession;

// One of the eight music track sets on the piano roll: the three streams
// it starts on Pulse 1, Pulse 2 and Triangle, as the engine plays them.
// Dragging a note rewrites its letter in the MML (and wraps it in octave
// commands when it leaves its octave), so every pass through that stream
// changes with it. The inspector picks the streams and channel parameters.
class CastleExcellentMusicModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    CastleExcellentMusicModel(CastleExcellentSession* session, int track);

    QString title() const override;
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
    QJsonObject music() const;
    const QVector<QVector<CastleExcellentMusicNote>>& traced() const;
    bool find(const QString& id, int* channel, int* index) const;
    QJsonObject trackSet() const;

    CastleExcellentSession* session_;
    int track_;
    int channel_ = 0;
    QString selected_;
    QSet<int> muted_;
    // A drag edits this copy of the music and writes once.
    bool editing_ = false;
    QJsonObject draft_;

    int draftVersion_ = 0;
    mutable QVector<QVector<CastleExcellentMusicNote>> trace_;
    mutable QString traceKey_;      // session revision, or the draft's version
};

// One MML sequence alone on the piano roll, on the voice that plays it (the
// triangle when a track set starts it there, else a pulse). Notes drag as in
// a track set; the inspector edits the whole MML text and what plays next.
class CastleExcellentSequenceModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    CastleExcellentSequenceModel(CastleExcellentSession* session, int stream);

    QString title() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    sdk::Budget budget() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · APU"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return 0; }
    void setCurrentChannel(int) override {}

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
    // `music` with track set 0 starting this sequence alone on its voice.
    QJsonObject solo(const QJsonObject& music) const;
    QString id() const;
    const QVector<CastleExcellentMusicNote>& traced() const;

    CastleExcellentSession* session_;
    int stream_;
    int voice_ = 0;                 // 0 pulse 1, 2 triangle
    QString selected_;
    bool editing_ = false;
    QJsonObject draft_;
    int draftVersion_ = 0;
    mutable QVector<CastleExcellentMusicNote> trace_;
    mutable QString traceKey_;
};

// The 21 MML streams as text: edit a stream's commands and where it
// restarts. The arena they share is fixed; the budget shows what is left.
class CastleExcellentStreamsModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit CastleExcellentStreamsModel(CastleExcellentSession* session);

    QString title() const override { return tr("MML streams"); }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    CastleExcellentSession* session_;
    int row_ = 0;
};

}  // namespace rl::castle_excellent
