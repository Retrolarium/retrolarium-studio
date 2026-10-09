#pragma once

#include "castle_excellent/data/levels.hpp"
#include "castle_excellent/render/room_painter.hpp"
#include "nes/editors.hpp"
#include "sdk/kit_session.hpp"

#include <QHash>
#include <QJsonObject>
#include <QPoint>

#include <functional>
#include <memory>

namespace rl::castle_excellent {

// The documents of a Castle Excellent project, swapped whole by undo. The title demo
// (documents/demo.json) is empty in projects made before it.
struct CastleExcellentState {
    CastleExcellentLevels levels;
    nes::Bundle graphics;
    QJsonObject demo;
    QJsonObject screens;    // documents/screens.json, empty before it is added

    bool operator==(const CastleExcellentState& other) const {
        return levels == other.levels && graphics == other.graphics && demo == other.demo &&
               screens == other.screens;
    }
};

// Where "Play from selected" starts: a room (1-100) and the player's pixel
// position in it.
// Where Play from selected starts: a room, and in it the player's top-left.
// The player stands where the user dropped the start marker in that room;
// without one (placed false) he starts at the default spot.
struct CastleExcellentStart {
    int room = 1;
    QPoint pixel{152, 136};
    bool placed = false;
};

// An open Castle Excellent / Castlequest project (`rldisasm preset`):
// the room document and the graphics bundle.
class CastleExcellentSession final : public sdk::KitSession, public nes::BundleOwner {
    Q_OBJECT

public:
    static std::unique_ptr<CastleExcellentSession> open(const sdk::ProjectFile& project, QString* error);

    sdk::Profile profile() const override;
    QVector<sdk::Kind> kinds() const override;
    QVector<sdk::DocumentGroup> documents(const QString& kind) const override;
    std::unique_ptr<sdk::EditorModel> openEditor(const QString& kind, const QString& document) override;
    bool save(QString* error) override;

    bool canBuild() const override { return true; }
    void build() override;
    bool canPlay() const override { return true; }
    void play() override;
    QString selectedStart() const override;
    void playSelected() override;
    QString playSelectedLabel() const override;

    // nes::BundleOwner
    sdk::Session* bundleSession() override { return this; }
    const nes::Bundle& bundle() const override { return state_.graphics; }
    const nes::Bundle& savedBundle() const override { return saved_.graphics; }
    bool editBundle(const QString& text, const std::function<void(nes::Bundle&)>& change, QString* error) override;
    QVector<nes::PreviewPalette> previewPalettes(const QString& chr, bool sprites) const override;
    QVector<sdk::PaletteSample> paletteSamples(const QString& table) const override;
    QStringList paletteRowLabels(const QString& table) const override;

    const CastleExcellentLevels& levels() const { return state_.levels; }
    const CastleExcellentLevels& savedLevels() const { return saved_.levels; }
    const CastleExcellentArt& art() const { return art_; }
    // Rebuilt when the documents change.
    const CastleExcellentPainter& painter() const;
    quint64 revision() const { return revision_; }

    // One undo step: the edit runs on a copy of both documents.
    bool edit(const QString& text, const std::function<void(CastleExcellentState&)>& change, QString* error);

    CastleExcellentStart start() const;
    // A placed start puts the room's marker at its pixel; an unplaced one only
    // picks the room.
    void setStart(const CastleExcellentStart& start);
    // Takes the marker out of `room`: the player starts at the default spot.
    void clearStart(int room);

    // The music resource of the graphics bundle (castle-excellent.mml-music.v1).
    QJsonObject music() const;
    QJsonObject savedMusic() const;
    bool editMusic(const QString& text, const QJsonObject& next, QString* error);

    // The screens document (ending, title and castle-map name tables, the
    // GAME OVER message); empty in projects made before it.
    const QJsonObject& screensDocument() const { return state_.screens; }
    const QJsonObject& savedScreens() const { return saved_.screens; }
    bool editScreens(const QString& text, const QJsonObject& next, QString* error);

    const QJsonObject& demo() const { return state_.demo; }
    const QJsonObject& savedDemo() const { return saved_.demo; }
    int demoCapacity() const;
    bool editDemo(const QString& text, const std::function<void(QJsonObject&)>& change, QString* error);
    // While a demo editor is open, Play from selected runs the demo.
    void attachDemoEditor(bool attach);
    bool demoStart() const { return demoEditors_ > 0; }
    // Builds and loops the demo, or records a new one from the controller.
    void playDemo(bool record);

signals:
    void startChanged();

private:
    CastleExcellentSession() = default;
    void setState(const CastleExcellentState& state, quint64 revision);
    void launch(bool fromStart);
    void launchDemo(bool record);
    void useColours(sdk::ProcessSpec& game) const;
    void applyRecording(const QString& path);
    // Runs the kit's castle-excellent-upgrade (title demo and screens), loads what it
    // added and opens `kind`/`document`.
    void upgradeDocuments(const QString& kind, const QString& document);

    QString levelsPath_;
    QString graphicsPath_;
    QString demoPath_;
    QString screensPath_;
    QString musicId_;
    int demoEditors_ = 0;
    CastleExcellentState state_;
    CastleExcellentState saved_;
    CastleExcellentArt art_;
    quint64 revision_ = 0;
    quint64 nextRevision_ = 1;
    CastleExcellentStart start_;                 // its room; pixels come from placedStarts_
    QHash<int, QPoint> placedStarts_;   // room -> where the user put the player

    mutable std::unique_ptr<CastleExcellentPainter> painter_;
    mutable quint64 painterRevision_ = ~quint64(0);
};

// "Room 7"
QString roomLabel(int room);

}  // namespace rl::castle_excellent
