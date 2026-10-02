#pragma once

#include "sdk/editor_model.hpp"
#include "smb/data/music.hpp"
#include "smb/data/resource_document.hpp"

#include <QSet>

#include <functional>
#include <optional>

namespace rl::smb {

class SmbSession;

// One SMB music header on the piano roll: square 2 (melody), square 1,
// triangle and noise. Notes change in place: pitch on the tonal channels,
// length where the note byte holds it (square 1, noise) or through the
// length byte that starts a run (square 2, triangle). Songs share stream
// bytes; the inspector names every song an edit reaches.
class SmbMusicModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    SmbMusicModel(SmbSession* session, const QString& header);

    QString title() const override { return header_; }
    sdk::Budget budget() const override;
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · APU"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;

    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override;
    int pitchMaximum() const override;
    int ticksPerBar() const override;

    QString selectedNote() const override { return selected_; }
    void selectNote(const QString& id) override;
    void beginEdit(const QString& id) override;
    bool editNote(const QString& id, int pitch, int length) override;
    void endEdit() override;

    bool canPreview() const override { return true; }
    void preview() override;
    void stopPreview() override;

private:
    const SmbMusic& music() const;
    const SmbMusicTimeline& timeline() const;
    // The event a note id names: channel index and event index.
    bool find(const QString& id, int* channel, int* event) const;
    int beatPitch(int beat) const;
    int lengthIndexFor(int duration) const;
    bool editMusic(const QString& text, const std::function<void(SmbMusic&)>& change, QString* error);
    QString selectorOf() const;

    SmbSession* session_;
    QString header_;
    int channel_ = 0;
    QString selected_;
    QSet<QString> muted_;

    std::optional<SmbResourceDocument> before_;
    quint64 beforeRevision_ = 0;
    bool changed_ = false;

    mutable SmbMusic music_;
    mutable SmbMusicTimeline timeline_;
    mutable QString timelineError_;
    mutable quint64 revision_ = ~quint64(0);
};

}  // namespace rl::smb
