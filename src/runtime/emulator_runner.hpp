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

    // Runs a game. While the same game runs (same program, working directory
    // and arguments apart from the cartridge file and the autoboot script),
    // it is hot-reloaded instead: the new build is mounted and the machine
    // hard-resets into it, with the new environment and ready file.
    void start(const sdk::ProcessSpec& spec, const QString& title,
               std::function<void(const sdk::ProcessResult&)> finished);
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
    // The picture as it is on screen, or null when nothing runs.
    QImage picture() const;
    // Saves the emulator picture as PNG; returns the path or empty.
    QString saveScreenshot(const QString& directory, QString* error);

signals:
    void stateChanged();
    void outputLine(const QString& line);

private:
    void append(const QString& line);

    QPointer<EmulatorSurface> surface_;
    void watchStartup();
    void setPhase(Phase phase);
    void writeKeys();
    void writePicture();
    void acknowledgeWarnings(const sdk::ProcessSpec& spec);
    bool writeControl(const sdk::ProcessSpec& spec, const QString& gameScript, QString* error);
    QString stageCart(const QString& source, QString* error);
    bool reload(const sdk::ProcessSpec& spec, const QString& cartSource, const QString& gameScript,
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
};

}  // namespace rl::runtime
