#pragma once

#include "sdk/types.hpp"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>
#include <memory>

class QProcess;
class QTemporaryDir;
class QTimer;

namespace rl::runtime {

// The native child window MAME draws into (-attach_window). MAME cannot read
// the keyboard of a window it does not own, so while the surface has focus it
// takes the keys itself and reports them by MAME's names; studio shortcuts
// and menus do not see them. Function keys and Pause stay with the studio.
class EmulatorSurface final : public QWidget {
    Q_OBJECT

public:
    explicit EmulatorSurface(QWidget* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void clicked();
    void keyChanged(const QString& key, bool pressed);  // "KEYCODE_LEFT"
    void focusChanged(bool focused);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;
};

// An input movie a run records or plays from power-on (Run > Movie). A
// recording is written as it goes and when the run ends; Resume plays the
// movie to its end, then pauses and records on.
struct MovieRequest {
    enum class Mode { None, Record, Play, Resume };
    Mode mode = Mode::None;
    QString file;      // the .rlmovie written or read
    QString origin;    // Play: the movie chosen, when `file` is a prepared copy
    QString header;    // a recording's header lines, each ending in a newline
    QString stateKey;  // the save-state folder for this image (its SHA-1)
    int stopAt = 0;            // Play: end the run at this frame (0: no)
    bool closeAtEnd = false;   // Play: end the run when the movie ends
};

// A RAM cell the watch overlay shows: its name and bytes of the main CPU.
struct WatchCell {
    QString name;
    quint32 address = 0;
    int length = 1;
};

// One emulator process at a time, rendering into a surface the Playtest view
// shows. Game-agnostic: the plugin supplies the command, this adds embedding.
class EmulatorRunner final : public QObject {
    Q_OBJECT

public:
    explicit EmulatorRunner(QObject* parent = nullptr);
    ~EmulatorRunner() override;

    // The view that shows the emulator provides the surface it draws into.
    void setSurface(EmulatorSurface* surface);
    EmulatorSurface* surface() const { return surface_; }
    // Starting lasts until the emulator's window appears; Stopping until the
    // process has gone.
    enum class Phase { Idle, Starting, Running, Stopping };
    Phase phase() const { return phase_; }
    // The game runs its start fast and unseen: views keep the picture covered
    // while Starting.
    bool hidesStart() const { return fastStart_; }
    // Starting is a hot reload of the running game.
    bool isReloading() const { return reloading_; }
    bool isRunning() const { return process_ != nullptr; }
    bool isPaused() const { return paused_; }
    QString title() const { return title_; }
    QStringList log() const { return log_; }
    // Forgets the output: another project's games are not this one's.
    void clearLog() { log_.clear(); }

    // Runs a game. While the same game runs (same program, working directory
    // and arguments apart from the cartridge file and the autoboot script),
    // it is hot-reloaded instead: the new build is mounted and the machine
    // hard-resets into it, with the new environment and ready file.
    // A movie run is never a hot reload: it starts the machine from power-on.
    void start(const sdk::ProcessSpec& spec, const QString& title,
               std::function<void(const sdk::ProcessResult&)> finished,
               const MovieRequest& movie = {});
    // Asks the emulator to close and returns; it is killed if it lingers.
    void stop();
    // Ends the emulator before returning.
    void stopNow();
    bool togglePause();
    void focusEmulator();
    // Forwarded keyboard: the keys held for the game, by MAME's names.
    void setKey(const QString& key, bool pressed);
    void releaseKeys();
    // How the picture is shown, applied live: square pixels instead of the
    // machine's 4:3 screen, and the studio's colours instead of the
    // emulator's when the launch gave a palette.
    void setPicture(bool squarePixels, bool studioColours);
    bool squarePixels() const { return squarePixels_; }
    bool studioColours() const { return studioColours_; }
    bool hasStudioPalette() const { return hasPalette_; }
    // Emulation controls, through the control script (a run without it has
    // only pause and stop). Speed in percent, 0 for no limit; a frame advance
    // pauses first; save states go to numbered slots (0-9) per image. None of
    // them changes what a movie records: it holds the input of every frame.
    bool hasControls() const { return controls_; }
    int speed() const { return speed_; }
    void setSpeed(int percent);
    void frameAdvance();
    int stateSlot() const { return slot_; }
    void setStateSlot(int slot);
    void saveState();
    void loadState();
    bool slotHasState(int slot) const;
    // The frame a slot's state was saved at (from power-on); -1 when unknown.
    int slotFrame(int slot) const;
    // The movie the run records or plays: "record", "play" or "none" (it
    // ended, or was stopped), its current frame and length.
    QString movieMode() const { return movieMode_; }
    int movieFrame() const { return movieFrame_; }
    int movieLength() const { return movieLength_; }
    const MovieRequest& movie() const { return movie_; }
    void stopMovie();
    // Back by frames, paused there: the run keeps states of its last
    // seconds and plays the frames after one again.
    void rewind(int frames);
    // Over the picture: pad one's buttons, and the RAM cells to watch (set
    // before a run starts; a game's movie checks name them).
    void setOverlay(bool input, bool watch);
    bool showsInput() const { return showInput_; }
    bool showsWatch() const { return showWatch_; }
    void setWatch(const QVector<WatchCell>& cells) { watch_ = cells; }
    // The TAS editor, on a movie that plays: go to a frame (paused there) or
    // play on from it, take the movie file in again after an edit (the states
    // from the first frame it changed on are dropped), go to a frame anew
    // from a state before it (an edit changed the frames that led there).
    // While it edits, the movie pauses at its end instead of handing the
    // pads over.
    void seek(int frame);
    void playFrom(int frame);
    void reloadMovie(int changedFrom = -1);
    void replay(int frame);
    void setMovieEditing(bool editing);
    // A short note over the game's picture; false when nothing can show it.
    bool showMessage(const QString& text);

    // The picture as it is on screen, or null when nothing runs.
    QImage picture() const;
    // Saves the emulator picture as PNG; returns the path or empty.
    QString saveScreenshot(const QString& directory, QString* error);

signals:
    void stateChanged();
    void outputLine(const QString& line);
    // The movie's frame or mode, the speed or the slot changed.
    void emulationChanged();
    // Something the control script did: "saved", "loaded", "no-state",
    // "ended", "stopped", "recording" (Resume reached the end),
    // "movie-end" (the edited movie paused at its end) or "foreign-state"
    // (a state from outside the movie ended it).
    void emulationEvent(const QString& event);

private:
    void append(const QString& line);
    void sendAction(const QString& verb, const QString& argument = QString());
    void readStatus();
    QString statePath(int slot) const;
    void finishMovie();

    QPointer<EmulatorSurface> surface_;
    void watchStartup();
    void setPhase(Phase phase);
    void writeKeys();
    void writeActions();
    void writePicture();
    void acknowledgeWarnings(const sdk::ProcessSpec& spec);
    bool writeControl(const sdk::ProcessSpec& spec, const QStringList& gameScripts, QString* error);
    QString stageCart(const QString& source, QString* error);
    bool reload(const sdk::ProcessSpec& spec, const QString& cartSource, const QStringList& gameScripts,
                const QString& title, std::function<void(const sdk::ProcessResult&)>& finished);

    QProcess* process_ = nullptr;
    Phase phase_ = Phase::Idle;
    QTimer* startupTimer_;
    int startupChecks_ = 0;
    QString readyFile_;
    bool fastStart_ = false;
    bool reloading_ = false;
    // The running game's temporary folder: control script, command file and
    // cartridge copies.
    std::unique_ptr<QTemporaryDir> stage_;
    QString cart_;                  // the copy MAME has mounted
    int cartSerial_ = 0;
    QSet<QString> keys_;
    QStringList signature_;         // what a launch must match to reload
    std::function<void(const sdk::ProcessResult&)> finished_;
    QString title_;
    QStringList log_;
    bool paused_ = false;
    bool squarePixels_ = true;
    bool studioColours_ = true;
    bool hasPalette_ = false;
    QImage pausedPicture_;
    bool stopping_ = false;
    // The control script's actions and status (see emulationChanged).
    bool controls_ = false;
    QTimer* statusTimer_;
    QStringList actions_;
    int actionSerial_ = 0;
    int speed_ = 100;
    int slot_ = 0;
    QString stateDir_;
    MovieRequest movie_;
    bool showInput_ = false;
    bool showWatch_ = false;
    QVector<WatchCell> watch_;
    QString movieMode_ = QStringLiteral("none");
    int movieFrame_ = 0;
    int movieLength_ = 0;
    int eventSerial_ = 0;
};

}  // namespace rl::runtime
