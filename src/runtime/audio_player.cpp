#include "runtime/audio_player.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace rl::runtime {

namespace {

quint32 le32(const QByteArray& bytes, int at) {
    const auto* d = reinterpret_cast<const uchar*>(bytes.constData()) + at;
    return quint32(d[0]) | quint32(d[1]) << 8 | quint32(d[2]) << 16 | quint32(d[3]) << 24;
}

// One pass of a PCM WAV in milliseconds, from its fmt and data chunks; 0
// when the file is not one.
qint64 wavDuration(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray head = file.read(4096);
    if (head.size() < 12 || !head.startsWith("RIFF") || head.mid(8, 4) != "WAVE") return 0;
    quint32 byteRate = 0;
    for (qint64 at = 12; at + 8 <= head.size();) {
        const QByteArray id = head.mid(int(at), 4);
        const quint32 size = le32(head, int(at) + 4);
        if (id == "fmt " && at + 20 <= head.size()) {
            byteRate = le32(head, int(at) + 16);
        } else if (id == "data") {
            return byteRate ? qint64(size) * 1000 / byteRate : 0;
        }
        // 64-bit, so a damaged chunk size ends the scan instead of looping.
        at += 8 + qint64(size) + (size & 1);
    }
    return 0;
}

}  // namespace

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent), passEnd_(new QTimer(this)) {
    passEnd_->setSingleShot(true);
    passEnd_->setTimerType(Qt::PreciseTimer);
    connect(passEnd_, &QTimer::timeout, this, &AudioPlayer::passEnded);
}

AudioPlayer::~AudioPlayer() { stop(); }

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
    durationMs_ = wavDuration(copy_);
    start();
    emit stateChanged(playing_);
}

void AudioPlayer::start() {
    startedLooping_ = looping_;
    clock_.start();
#ifdef Q_OS_WIN
    const std::wstring path = QDir::toNativeSeparators(copy_).toStdWString();
    // Windows repeats a looping sound itself, without a gap between passes.
    const DWORD flags = SND_FILENAME | SND_ASYNC | SND_NODEFAULT | (looping_ ? SND_LOOP : 0);
    playing_ = PlaySoundW(path.c_str(), nullptr, flags) != FALSE;
#else
    if (fallback_) {
        fallback_->disconnect(this);
        fallback_->kill();
        fallback_->deleteLater();
    }
    fallback_ = new QProcess(this);
    const QString player = QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
                               ? QStringLiteral("aplay")
                               : QStringLiteral("paplay");
    connect(fallback_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this] {
        if (looping_) {
            start();
            return;
        }
        playing_ = false;
        emit stateChanged(false);
    });
    fallback_->start(player, {copy_});
    playing_ = true;
#endif
    armPassEnd();
}

void AudioPlayer::armPassEnd() {
    passEnd_->stop();
#ifdef Q_OS_WIN
    // Windows tells nothing when a sound ends: time the pass instead, to
    // stop a loop after it, start one, or note that a single pass is over.
    // A sound started looping that still loops needs nothing.
    if (playing_ && durationMs_ > 0 && !(looping_ && startedLooping_)) {
        const qint64 into = clock_.elapsed() % durationMs_;
        passEnd_->start(int(durationMs_ - into));
    }
#endif
}

void AudioPlayer::passEnded() {
#ifdef Q_OS_WIN
    if (!playing_) return;
    if (looping_ && !startedLooping_) {
        start();  // the single pass ends: carry on looping from here
        return;
    }
    if (startedLooping_) {
        PlaySoundW(nullptr, nullptr, 0);  // the loop was switched off: end with this pass
    }
    playing_ = false;
    emit stateChanged(false);
#endif
}

void AudioPlayer::setLooping(bool loop) {
    if (loop == looping_) return;
    looping_ = loop;
    armPassEnd();
}

void AudioPlayer::stop() {
    passEnd_->stop();
#ifdef Q_OS_WIN
    if (playing_) {
        PlaySoundW(nullptr, nullptr, 0);
    }
#else
    if (fallback_) {
        fallback_->disconnect(this);
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
