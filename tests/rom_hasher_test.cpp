#include "runtime/rom_hasher.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

using rl::runtime::RomHasher;
namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
bool waitFor(const std::function<bool()>& ready, int timeout = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return ready();
}
}  // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const QString path = temporary.filePath(QStringLiteral("rom.bin"));
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly), "create ROM");
    file.write("abc");
    file.close();
    RomHasher hasher;
    int calls = 0;
    QString digest, error;
    bool tooLarge = false;
    QObject::connect(&hasher, &RomHasher::finished, &app,
                     [&](const QString&, const QString& value, const QString& failure, bool large) {
                         ++calls;
                         digest = value;
                         error = failure;
                         tooLarge = large;
                     });
    expect(hasher.start(path, 3), "start read");
    expect(calls == 0, "completion is asynchronous");
    expect(!hasher.start(path, 3), "a second read is refused while busy");
    expect(waitFor([&] { return calls == 1; }), "read completes");
    expect(digest == QLatin1String("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256");
    expect(error.isEmpty() && !tooLarge && !hasher.isBusy(), "successful state");
    hasher.start(path, 2);
    expect(waitFor([&] { return calls == 2; }) && tooLarge && digest.isEmpty(), "byte limit");
    hasher.start(temporary.filePath(QStringLiteral("absent")), 3);
    expect(waitFor([&] { return calls == 3; }) && !error.isEmpty(), "read error");
    hasher.start(temporary.path(), 3);
    expect(waitFor([&] { return calls == 4; }) && !error.isEmpty() && digest.isEmpty(), "directories are refused");
#ifndef Q_OS_WIN
    const QString fifo = temporary.filePath(QStringLiteral("fifo.nes"));
    expect(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0, "create a FIFO without a writer");
    hasher.start(fifo, 3);
    expect(waitFor([&] { return calls == 5; }) && !error.isEmpty() && !hasher.isBusy(),
           "FIFO is refused without opening it");
#endif

    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    auto returned = std::make_shared<QSemaphore>();
    auto workerThread = std::make_shared<std::atomic<QThread*>>(nullptr);
    RomHasher slow(
        nullptr, [entered, release, returned, workerThread](const QString& name, qint64, const RomHasher::Cancelled&) {
            workerThread->store(QThread::currentThread());
            if (name == QLatin1String("old")) {
                entered->release();
                release->tryAcquire(1, 3000);
                returned->release();
            }
            return RomHasher::Result{name, {}, false};
        });
    QStringList answers;
    QObject::connect(&slow, &RomHasher::finished, &app,
                     [&](const QString&, const QString& value, const QString&, bool) { answers << value; });
    slow.start(QStringLiteral("old"), 3);
    bool heartbeat = false;
    QTimer::singleShot(0, &app, [&] { heartbeat = true; });
    expect(waitFor([&] { return entered->available() && heartbeat; }), "UI events run during a blocked read");
    expect(workerThread->load() != app.thread(), "hash runs outside the UI thread");
    slow.cancel();
    expect(!slow.isBusy(), "cancel returns immediately");
    slow.start(QStringLiteral("new"), 3);
    expect(waitFor([&] { return answers.size() == 1; }), "new request completes");
    release->release();
    expect(waitFor([&] { return returned->available(); }), "old worker ends");
    waitFor([&] { return slow.findChildren<QObject*>().isEmpty(); }, 100);
    expect(answers == QStringList{QStringLiteral("new")}, "cancelled completion cannot reopen an old ROM");
    if (failures) return 1;
    std::cout << "ROM hash: asynchronous reads, bounds and cancellation hold\n";
    return 0;
}
