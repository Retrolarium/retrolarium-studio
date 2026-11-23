#include "runtime/rom_hasher.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace rl::runtime {

RomHasher::RomHasher(QObject* parent, Hash hash) : QObject(parent), hash_(hash ? std::move(hash) : calculate) {}

RomHasher::~RomHasher() {
    if (cancelled_) cancelled_->store(true);
}

RomHasher::Result RomHasher::calculate(const QString& path, qint64 maxBytes, const Cancelled& cancelled) {
    if (!QFileInfo(path).isFile()) return {{}, tr("The selected path is not a regular file."), false};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, file.errorString(), false};
    if (file.size() > maxBytes) return {{}, {}, true};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    while (!file.atEnd()) {
        if (cancelled->load()) return {};
        const QByteArray chunk = file.read(64 * 1024);
        if (file.error() != QFileDevice::NoError) return {{}, file.errorString(), false};
        total += chunk.size();
        // Also bound a file that grew after its size was checked.
        if (total > maxBytes) return {{}, {}, true};
        hash.addData(chunk);
    }
    return {QString::fromLatin1(hash.result().toHex()), {}, false};
}

bool RomHasher::start(const QString& path, qint64 maxBytes) {
    if (isBusy()) return false;
    auto* watcher = new QFutureWatcher<Result>(this);
    pending_ = watcher;
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const Cancelled cancelled = cancelled_;
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        if (pending_ != watcher) return;
        pending_ = nullptr;
        cancelled_.reset();
        const Result result = watcher->result();
        emit stateChanged();
        emit finished(path, result.digest, result.error, result.tooLarge);
    });
    const Hash hash = hash_;
    watcher->setFuture(
        QtConcurrent::run([hash, path, maxBytes, cancelled] { return hash(path, maxBytes, cancelled); }));
    emit stateChanged();
    return true;
}

void RomHasher::cancel() {
    if (!pending_) return;
    cancelled_->store(true);
    pending_ = nullptr;
    cancelled_.reset();
    emit stateChanged();
}

}  // namespace rl::runtime
