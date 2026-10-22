#pragma once

#include "sdk/editor_model.hpp"

#include <QJsonObject>
#include <QSet>
#include <QStringList>

#include <memory>

namespace rl::sdk {
class Session;
}

namespace rl::nes {

class BundleOwner;

// A sound effect as the session lists it: the kit's effect key
// ("smb:square2:1", "castle-excellent:3", "doraemon:0:12") and what to call it.
struct SoundEffect {
    QString key;
    QString title;
    QString trigger;  // how the game starts it ("Square 2 queue · $01")
    QString note;     // extra inspector text, optional
};

// The project data an effect plays with: tables the session edits. The
// effect is traced and heard with the unsaved state.
class SoundEffectData {
public:
    virtual ~SoundEffectData() = default;

    // What the effect depends on; a change traces it again.
    virtual QJsonObject state() const = 0;
    // Whether that differs from the saved project.
    virtual bool edited() const = 0;
    // The whole unsaved resource bundle, for the kit's --document.
    virtual bool writeDocument(const QString& path, QString* error) const = 0;
    // One byte of a table the trace names as a note's source; false: the
    // effect's tables are not editable here.
    virtual bool setTableValue(const QString& table, int index, int value, const QString& text, QString* error);
};

// Bundle resources of a nes::BundleOwner session (Castle Excellent's SFX parameters).
std::shared_ptr<SoundEffectData> bundleSoundEffectData(BundleOwner* owner, QStringList resources);

// One NES sound effect played through the game's own driver: the kit runs
// the effect's routines frame by frame and records the APU (audio
// render-sfx). The piano roll shows what each channel played, one tick per
// frame. The effects are code; a note the trace sets from a table byte can
// be dragged to another pitch the table can hold, which edits that byte.
class SoundEffectModel final : public sdk::SequencerModel {
    Q_OBJECT

public:
    SoundEffectModel(sdk::Session* session, SoundEffect effect, QString projectPath, QString projectDirectory,
                     std::shared_ptr<SoundEffectData> data = {});

    QString title() const override { return effect_.title; }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QString statusText() const override;
    bool isReadOnly() const override { return !hasTables(); }
    QString readOnlyReason() const override;

    QString chipLabel() const override { return QStringLiteral("2A03 · APU"); }
    QVector<sdk::SequencerChannel> channels() const override;
    int currentChannel() const override { return channel_; }
    void setCurrentChannel(int channel) override;
    void setChannelMuted(int channel, bool muted) override;

    QVector<sdk::SequencerNote> notes(int channel) const override;
    int pitchMinimum() const override { return 24; }
    int pitchMaximum() const override { return 108; }
    int ticksPerBar() const override { return 15; }

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
    QString outputPath(const QString& suffix) const;
    // Kit arguments for this effect, with the unsaved bundle when the
    // effect's data is edited.
    QStringList arguments(QString* error) const;
    void requestTrace();
    void report(const QString& message) const;

    bool hasTables() const;
    QJsonObject noteAt(const QString& id) const;
    QJsonObject tableOf(const QString& table) const;  // the trace's table entry
    // The table value that plays `pitch` (keeping a noise byte's mode bit),
    // or -1.
    int valueFor(const QJsonObject& source, int pitch) const;
    int nearestPitch(const QJsonObject& source, int pitch) const;
    bool setSourceValue(const QJsonObject& source, int value, QString* error);

    sdk::Session* session_;
    SoundEffect effect_;
    QString projectPath_;
    QString projectDirectory_;
    std::shared_ptr<SoundEffectData> data_;
    QJsonObject trace_;
    QJsonObject tracedState_;
    bool tracing_ = false;
    int channel_ = 0;
    QString selected_;
    QSet<int> muted_;
    QString dragging_;
    int dragPitch_ = -1;
};

}  // namespace rl::nes
