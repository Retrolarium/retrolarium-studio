#include "runtime/audio_player.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace rl::runtime {

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {}

AudioPlayer::~AudioPlayer() {
    stop();
}

void AudioPlayer::play(const QString& wavPath) {
    stop();
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/retrolarium-audio");
    QDir().mkpath(directory);
    copy_ = QDir(directory).filePath(QStringLiteral("preview.wav"));
    QFile::remove(copy_);
    if (!QFile::copy(wavPath, copy_)) {
        copy_ = wavPath;
    }
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(copy_).toStdWString();
    playing_ = PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
#else
    fallback_ = new QProcess(this);
    const QString player = QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                               ? QStringLiteral("aplay")
                               : QStringLiteral("paplay");
    connect(fallback_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this] {
        playing_ = false;
        emit stateChanged(false);
    });
    fallback_->start(player, {copy_});
    playing_ = true;
#endif
    emit stateChanged(playing_);
}

void AudioPlayer::stop() {
#ifdef Q_OS_WIN
    if (playing_) {
        PlaySoundW(nullptr, nullptr, 0);
    }
#else
    if (fallback_) {
        fallback_->kill();
        fallback_->deleteLater();
        fallback_ = nullptr;
    }
#endif
    if (playing_) {
        playing_ = false;
        emit stateChanged(false);
    }
}

}  // namespace rl::runtime
