#pragma once

#include "flicky/data/sound_document.hpp"
#include "sdk/editor_model.hpp"

#include <QHash>

#include <optional>

namespace rl::flicky {

class FlickySession;

// One music or sound-effect header: its tracks as channels, each track's
// sequence as notes. Pitch edits rewrite the note byte (0x81 + pitch);
// length edits rewrite the duration byte, which later notes may share.
class SoundHeaderModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    SoundHeaderModel(FlickySession* session, const QString& id);

    QString title() const override { return id_; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QString chipLabel() const override { return QStringLiteral("YM2612 + PSG"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;
    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMaximum() const override { return 94; }
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
    FlickySoundHeader header() const;
    const QVector<FlickySoundNote>& sequenceNotes(const QString& sequence) const;
    // Note id "<channel>:<offset>".
    bool findNote(const QString& id, int* channel, FlickySoundNote* note) const;

    FlickySession* session_;
    QString id_;
    int channel_ = 0;
    QString selected_;
    std::optional<FlickySoundDocument> before_;
    quint64 beforeRevision_ = 0;
    bool changed_ = false;
    mutable QHash<QString, QVector<FlickySoundNote>> notes_;
    mutable quint64 notesRevision_ = ~quint64(0);
};

// One FM voice bank: four operator rows per voice.
class FmVoiceModel final : public sdk::TableModel {
    Q_OBJECT

public:
    FmVoiceModel(FlickySession* session, const QString& bank);

    QString title() const override { return bank_; }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    FlickySession* session_;
    QString bank_;
    int row_ = 0;
};

// The 63 priority and envelope bytes of the sound driver.
class EnvelopeModel final : public sdk::TableModel {
    Q_OBJECT

public:
    explicit EnvelopeModel(FlickySession* session);

    QString title() const override { return tr("Priority & envelopes"); }
    sdk::Inspection inspect() const override;
    QString statusText() const override;

    QVector<sdk::TableColumn> columns() const override;
    int rowCount() const override;
    QString cell(int row, int column) const override;
    bool isRowDirty(int row) const override;
    int currentRow() const override { return row_; }
    void setCurrentRow(int row) override;
    bool setCell(int row, int column, const QString& value, QString* error) override;

private:
    FlickySession* session_;
    int row_ = 0;
};

}  // namespace rl::flicky
