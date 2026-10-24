#pragma once

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace rl::shell {

class MainWindow;

// A scripted run of the studio for the interface tests (tools/autotest): it
// does what a user does, through the same actions and editor models the
// clicks reach, and records what each step showed.
//
//   retrolarium-studio --autotest scenario.json
//
// The scenario names a ROM to open (Open ROM, so a known game's repository
// is downloaded and readied as for a user), the folder for the results
// (results.json and a screenshot of every step) and how far to go:
// documents per kind, edits on each, then save, build and play from the
// selected start. Settings it lists are written first (the run's own
// RETROLARIUM_STUDIO_HOME keeps the user's untouched). A dialog no step
// expects is captured and dismissed. The studio exits with 0 when every
// step held, 1 otherwise.
class Autotest final : public QObject {
    Q_OBJECT

public:
    Autotest(MainWindow* window, const QString& scenarioPath);

    void start();

private:
    using Step = std::function<void()>;

    // Runs `then` once `ready` holds, polling; after `timeoutMs` records the
    // failure `what` and runs `then` anyway (the run goes on).
    void waitFor(const std::function<bool()>& ready, int timeoutMs, const QString& what, const Step& then);
    // Runs `then` after `ms`. An exception out of a step is recorded and ends
    // it: the tour goes on with the next document, a later step ends the run.
    void after(int ms, const Step& then);

    void openRom();
    void tourDocuments();
    void visitNext();
    void saveBuildAndPlay();
    void finish();

    // Edits through the open editor's model; each is recorded.
    void editDocument(QJsonArray& edits);
    void record(QJsonArray& edits, const QString& what, bool ok, const QString& error = QString());

    QString screenshot(const QString& name);
    QJsonArray takeMessages();
    void watchDialogs();
    void note(const QString& kind, const QString& text);

    MainWindow* window_;
    QJsonObject scenario_;
    QDir output_;
    QJsonObject results_;
    QJsonArray events_;
    QJsonArray documents_;
    QVector<QPair<QString, QString>> queue_;  // kind, document
    int shot_ = 0;
    int seenMessages_ = 0;
    int dialogs_ = 0;  // dialogs seen and closed
    bool failed_ = false;
    bool touring_ = false;  // visiting the documents
    QString current_;       // the document being visited
    QElapsedTimer clock_;
};

}  // namespace rl::shell
