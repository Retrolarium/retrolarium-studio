#pragma once

#include <QObject>
#include <QString>

class QProcess;

namespace rl::runtime {

// Plays one WAV file at a time, asynchronously.
class AudioPlayer final : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    void play(const QString& wavPath);
    void stop();
    bool isPlaying() const { return playing_; }

signals:
    void stateChanged(bool playing);

private:
    QString copy_;      // a private copy, so the source can be rebuilt meanwhile
    bool playing_ = false;
    QProcess* fallback_ = nullptr;
};

}  // namespace rl::runtime
