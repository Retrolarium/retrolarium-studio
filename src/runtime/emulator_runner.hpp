#pragma once

#include "sdk/types.hpp"

#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>

class QProcess;
class QTimer;

namespace rl::runtime {

// The native child window MAME draws into (-attach_window).
class EmulatorSurface final : public QWidget {
    Q_OBJECT

public:
    explicit EmulatorSurface(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
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
    bool isRunning() const { return process_ != nullptr; }
    bool isPaused() const { return paused_; }
    QString title() const { return title_; }
    QStringList log() const { return log_; }

    void start(const sdk::ProcessSpec& spec, const QString& title,
               std::function<void(const sdk::ProcessResult&)> finished);
    // Asks the emulator to close and returns; it is killed if it lingers.
    void stop();
    // Ends the emulator before returning.
    void stopNow();
    bool togglePause();
    void focusEmulator();
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

    QProcess* process_ = nullptr;
    Phase phase_ = Phase::Idle;
    QTimer* startupTimer_;
    int startupChecks_ = 0;
    QString readyFile_;
    std::function<void(const sdk::ProcessResult&)> finished_;
    QString title_;
    QStringList log_;
    bool paused_ = false;
    bool stopping_ = false;
};

}  // namespace rl::runtime
