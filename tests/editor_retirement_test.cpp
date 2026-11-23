#include "editors/editor_retirement.hpp"
#include "editors/emulator_view.hpp"
#include "runtime/emulator_runner.hpp"
#include "runtime/mame_config.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/session.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
bool waitFor(const std::function<bool()>& ready, int timeout = 10000) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return ready();
}
QString python() {
    for (const auto& name : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const auto executable = QStandardPaths::findExecutable(name);
        if (executable.isEmpty()) continue;
        QProcess probe;
        probe.start(executable, {QStringLiteral("-c"), QStringLiteral("print(1)")});
        if (probe.waitForFinished(5000) && probe.exitCode() == 0) return executable;
    }
    return {};
}

class Session final : public rl::sdk::Session {
public:
    explicit Session(bool* alive) : alive_(alive) { *alive_ = true; }
    ~Session() override { *alive_ = false; }
    rl::sdk::Profile profile() const override { return {}; }
    QVector<rl::sdk::Kind> kinds() const override { return {}; }
    QVector<rl::sdk::DocumentGroup> documents(const QString&) const override { return {}; }
    std::unique_ptr<rl::sdk::EditorModel> openEditor(const QString&, const QString&) override { return {}; }
    bool save(QString*) override { return true; }

private:
    bool* alive_;
};

class Model final : public rl::sdk::EmulatorModel {
public:
    Model(bool* alive, const bool* sessionAlive) : alive_(alive), sessionAlive_(sessionAlive) { *alive_ = true; }
    ~Model() override {
        expect(*sessionAlive_, "a model is destroyed before its session");
        *alive_ = false;
    }
    QString title() const override { return QStringLiteral("Probe"); }
    rl::sdk::Inspection inspect() const override { return {}; }
    QSize screenSize() const override {
        expect(*sessionAlive_, "a surviving view reads a live session");
        return {256, 240};
    }
    QString coreLabel() const override { return QStringLiteral("Probe"); }
    QString startLabel() const override { return QStringLiteral("Probe"); }
    void play() override {}

private:
    bool* alive_;
    const bool* sessionAlive_;
};
}  // namespace

int main(int argc, char** argv) {
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString child = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString interpreter = python();
    if (interpreter.isEmpty()) return 77;
    rl::runtime::mame::rememberFeatures(interpreter, child, {});
    QTemporaryDir temporary;
    QWidget window;
    rl::runtime::EmulatorRunner runner;
    rl::editors::EditorRetirement retirement(&runner);
    const auto spec = [&](const QString& mode, const QString& ready) {
        rl::sdk::ProcessSpec result;
        result.program = interpreter;
        result.workingDirectory = temporary.path();
        result.arguments = QStringList{child, mode, ready};
        return result;
    };
    rl::runtime::MovieRequest movie;
    movie.mode = rl::runtime::MovieRequest::Mode::Record;
    movie.file = temporary.filePath(QStringLiteral("record.rlmovie"));

    for (const auto& mode : {QStringLiteral("record"), QStringLiteral("stall")}) {
        bool sessionAlive = false;
        bool modelAlive = false;
        std::unique_ptr<rl::sdk::Session> session = std::make_unique<Session>(&sessionAlive);
        std::unique_ptr<rl::sdk::EditorModel> model = std::make_unique<Model>(&modelAlive, &sessionAlive);
        std::unique_ptr<rl::editors::EditorView> view =
            std::make_unique<rl::editors::EmulatorView>(static_cast<Model*>(model.get()), &runner, nullptr, &window);
        QPointer<rl::editors::EditorView> oldView(view.get());
        auto* oldSurface = runner.surface();
        const auto nativeWindow = oldSurface->winId();
        const QString ready = temporary.filePath(mode + QStringLiteral(".ready"));
        int oldCalls = 0;
        runner.start(spec(mode, ready), mode, [&](const rl::sdk::ProcessResult&) { ++oldCalls; }, movie);
        expect(waitFor([&] { return QFile::exists(ready) && runner.movieMode() == QLatin1String("record"); }),
               "recording starts before project close");
        QElapsedTimer call;
        call.start();
        retirement.release(std::move(view), std::move(model));
        retirement.releaseSession(std::move(session));
        expect(call.elapsed() < 250, "closing a project returns before movie flush");
        expect(oldView && retirement.isPending() && modelAlive && sessionAlive,
               "the native view, model and session survive asynchronous stop");
        expect(runner.surface() == oldSurface && oldSurface->winId() == nativeWindow,
               "retirement preserves the exact native window");

        bool nextSessionAlive = false;
        bool nextModelAlive = false;
        auto nextSession = std::make_unique<Session>(&nextSessionAlive);
        auto nextModel = std::make_unique<Model>(&nextModelAlive, &nextSessionAlive);
        auto nextView = std::make_unique<rl::editors::EmulatorView>(nextModel.get(), &runner, nullptr, &window);
        expect(runner.surface() == oldSurface, "a new project does not steal the retiring native surface");
        const QString nextReady = temporary.filePath(mode + QStringLiteral("-next.ready"));
        int nextCalls = 0;
        runner.start(spec(QStringLiteral("plain"), nextReady), QStringLiteral("next"),
                     [&](const rl::sdk::ProcessResult&) { ++nextCalls; });
        int beats = 0;
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++beats; });
        timer.start();
        expect(waitFor([&] { return QFile::exists(nextReady); }), "the next project starts after retirement");
        timer.stop();
        expect(beats > 0 && !oldView && !retirement.isPending() && !modelAlive && !sessionAlive,
               "the UI runs and retired objects are released after process exit");
        expect(oldCalls == 0, "closed project callbacks are discarded");
        expect(runner.surface() && nextView->isAncestorOf(runner.surface()),
               "old view destruction cannot detach the replacement's surface");
        runner.stop();
        expect(waitFor([&] { return !runner.isRunning() && nextCalls == 1; }), "the new project stops normally");
        nextView.reset();
        nextModel.reset();
        nextSession.reset();
        expect(!runner.surface(), "the idle view releases only its own surface");
    }
    // An application shutdown is allowed to wait, before its native windows go.
    runner.stopNow();
    if (failures) return 1;
    std::cout << "editor retirement: responsive close, native surfaces, session lifetime and replacement hold\n";
    return 0;
}
