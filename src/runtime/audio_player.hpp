#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace rl::runtime {

// Plays one WAV file at a time, asynchronously, once or over and over.
class AudioPlayer final : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    void play(const QString& wavPath);
    void stop();
    bool isPlaying() const { return playing_; }
    // Repeats the sound until stopped. Changed while playing, it applies
    // when the current pass ends, so the music never jumps back.
    void setLooping(bool loop);
    bool isLooping() const { return looping_; }

signals:
    void stateChanged(bool playing);

private:
    // Starts the copy from its beginning, looping or once.
    void start();
    // Arms the timer for the end of the current pass.
    void armPassEnd();
    void passEnded();

    QString copy_;      // a private copy, so the source can be rebuilt meanwhile
    bool playing_ = false;
    bool looping_ = false;
    bool startedLooping_ = false;   // how the sound playing now was started
    qint64 durationMs_ = 0;         // one pass; 0 when the header is unreadable
    QElapsedTimer clock_;           // since the sound playing now started
    QTimer* passEnd_ = nullptr;
    QProcess* fallback_ = nullptr;
};

}  // namespace rl::runtime
