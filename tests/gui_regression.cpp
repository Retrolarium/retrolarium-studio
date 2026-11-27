#include "library_layout_checks.hpp"
#include "builtin_plugins.hpp"
#include "editors/library_sheet.hpp"
#include "editors/tilemap_editor.hpp"
#include "frame/dll_search.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/kit_session.hpp"
#include "shell/main_window.hpp"
#include "shell/settings_dialog.hpp"
#include "theme/brand.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/document_list.hpp"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMap>
#include <QWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtTest/QtTest>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace rl::shell {
namespace {

constexpr int kPollMs = 25;

QString severityName(sdk::Severity severity) {
    switch (severity) {
        case sdk::Severity::Error:
            return QStringLiteral("error");
        case sdk::Severity::Warning:
            return QStringLiteral("warning");
        default:
            return QStringLiteral("info");
    }
}

QString buttonText(const QAbstractButton* button) {
    QString text = button ? button->text() : QString();
    text.remove(QLatin1Char('&'));
    return text;
}

QAbstractButton* findButton(QWidget* root, const QString& objectName, const QString& text) {
    if (!root) return nullptr;
    if (!objectName.isEmpty()) {
        if (auto* button = root->findChild<QAbstractButton*>(objectName)) return button;
    }
    for (QAbstractButton* button : root->findChildren<QAbstractButton*>()) {
        if (buttonText(button) == text) return button;
    }
    return nullptr;
}

QString dialogText(QWidget* dialog) {
    QStringList parts;
    if (dialog) parts << dialog->windowTitle();
    for (const QLabel* label : dialog ? dialog->findChildren<QLabel*>() : QList<QLabel*>()) {
        if (!label->text().trimmed().isEmpty()) parts << label->text().trimmed();
    }
    return parts.join(QStringLiteral(" | "));
}

QString sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

}  // namespace

// Drives the private, ROM-dependent GUI scenario. All writes go through real
// widgets. Access to MainWindow's private state is read-only and used for
// assertions, bounded waits and coordinates derived from the visible editor.
class GuiRegression final : public QObject {
public:
    GuiRegression(MainWindow* window, const QJsonObject& scenario)
        : QObject(window),
          window_(window),
          scenario_(scenario),
          output_(scenario.value(QStringLiteral("output")).toString()) {
        output_.mkpath(QStringLiteral("."));
        results_.insert(QStringLiteral("scenario"), scenario_);
        results_.insert(QStringLiteral("input_sha256"), scenario_.value(QStringLiteral("input_sha256")));
        results_.insert(QStringLiteral("expected_sha256"), scenario_.value(QStringLiteral("expected_sha256")));
        results_.insert(QStringLiteral("native_windows_file_dialog_covered"), false);
        dialogTimer_ = new QTimer(this);
        dialogTimer_->setInterval(kPollMs);
        connect(dialogTimer_, &QTimer::timeout, this, &GuiRegression::handleDialog);
        clock_.start();
    }

    void start() {
        // The timer also runs inside a modal dialog's nested exec() loop.
        dialogTimer_->start();
        QTimer::singleShot(0, this, [this] { run(); });
    }

private:
    void note(const QString& kind, const QString& text) {
        events_.append(QJsonObject{{QStringLiteral("at_ms"), double(clock_.elapsed())},
                                   {QStringLiteral("kind"), kind},
                                   {QStringLiteral("text"), text}});
    }

    void begin(const QString& step) {
        lastStep_ = step;
        note(QStringLiteral("step"), step);
        writeResults(QStringLiteral("RUNNING"), QString());
    }

    void verifyLibraryLayout(const QString& stage) {
        if (!window_->model_) return;
        const sdk::Library library = window_->model_->inspect().library;
        if (library.grid || library.items.isEmpty()) return;
        QStringList ids;
        for (const sdk::LibraryItem& item : library.items) ids.append(item.id);
        const auto layout = rl::tests::inspectLibraryLayout(window_, ids);
        QJsonArray checks = results_.value(QStringLiteral("library_layout_checks")).toArray();
        checks.append(QJsonObject{{QStringLiteral("stage"), stage},
                                  {QStringLiteral("card_count"), layout.cards.size()},
                                  {QStringLiteral("overlap_free"), layout.error.isEmpty()},
                                  {QStringLiteral("error"), layout.error},
                                  {QStringLiteral("cards"), layout.cards}});
        results_.insert(QStringLiteral("library_layout_checks"), checks);
        if (!layout.error.isEmpty()) throw std::runtime_error(layout.error.toStdString());
    }

    QString screenshot(const QString& name, QWidget* widget = nullptr) {
        if (name != QLatin1String("failure") && !widget && !QApplication::activeModalWidget())
            verifyLibraryLayout(name);
        const QString file = QStringLiteral("%1-%2.png").arg(shot_++, 3, 10, QLatin1Char('0')).arg(name);
        QWidget* shown = widget ? widget : QApplication::activeModalWidget();
        if (!shown) shown = window_;
        if (!shown->grab().save(output_.filePath(file), "PNG")) {
            throw std::runtime_error(QStringLiteral("Could not save screenshot %1").arg(file).toStdString());
        }
        screenshots_.append(QJsonObject{{QStringLiteral("step"), lastStep_}, {QStringLiteral("file"), file}});
        return file;
    }

    void rejectDialog(QWidget* modal, const QString& error) {
        dialogError_ = error;
        note(QStringLiteral("error"), error);
        if (auto* dialog = qobject_cast<QDialog*>(modal)) {
            dialog->reject();
        } else if (modal) {
            modal->close();
        }
    }

    void handleDialog() {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal || modal == window_ || modal == handledModal_) return;
        try {
            if (auto* dialog = qobject_cast<QFileDialog*>(modal)) {
                if (nextFilePath_.isEmpty()) {
                    rejectDialog(modal, QStringLiteral("Unexpected file dialog: %1").arg(dialogText(modal)));
                    return;
                }
                handledModal_ = modal;
                const QString path = nextFilePath_;
                nextFilePath_.clear();
                auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
                if (!name) throw std::runtime_error("The Qt file dialog has no filename field");
                QTest::mouseClick(name, Qt::LeftButton, Qt::NoModifier, name->rect().center());
                QTest::keyClick(name, Qt::Key_A, Qt::ControlModifier);
                QTest::keyClicks(name, QDir::toNativeSeparators(path));
                auto* box = dialog->findChild<QDialogButtonBox*>();
                QAbstractButton* open = box ? box->button(QDialogButtonBox::Open) : nullptr;
                if (!open) open = findButton(dialog, QString(), QStringLiteral("Open"));
                if (!open) throw std::runtime_error("The Qt file dialog has no Open button");
                QTest::mouseClick(open, Qt::LeftButton, Qt::NoModifier, open->rect().center());
                return;
            }
            QAbstractButton* source =
                findButton(modal, QStringLiteral("setUpGameSource"), QStringLiteral("Set up the source"));
            if (source) {
                if (!source->isVisible() || !source->isEnabled()) return;
                handledModal_ = modal;
                note(QStringLiteral("dialog"), dialogText(modal));
                clickButton(source, QStringLiteral("Set up the source"));
                sourceChoiceHandled_ = true;
                return;
            }
            if (modal->windowTitle() == QLatin1String("Run the game's scripts")) {
                QAbstractButton* allow = findButton(modal, QString(), QStringLiteral("Run scripts"));
                if (!allow || !allow->isVisible() || !allow->isEnabled()) return;
                handledModal_ = modal;
                note(QStringLiteral("dialog"), dialogText(modal));
                clickButton(allow, QStringLiteral("Run scripts"));
                consentHandled_ = true;
                note(QStringLiteral("step"), QStringLiteral("Allow the pinned repository scripts through the dialog"));
                return;
            }
            rejectDialog(modal, QStringLiteral("Unexpected modal dialog: %1").arg(dialogText(modal)));
        } catch (const std::exception& error) {
            rejectDialog(modal, QString::fromUtf8(error.what()));
        } catch (...) {
            rejectDialog(modal, QStringLiteral("Unknown modal dialog failure"));
        }
    }

    bool waitFor(const QString& what, int timeoutSeconds, const std::function<bool()>& ready) {
        QElapsedTimer waited;
        waited.start();
        while (!ready()) {
            checkEditDiagnostics();
            if (!dialogError_.isEmpty()) {
                const QString error = dialogError_;
                dialogError_.clear();
                throw std::runtime_error(error.toStdString());
            }
            if (waited.elapsed() >= timeoutSeconds * 1000) {
                throw std::runtime_error(
                    QStringLiteral("Timed out after %1 s waiting for %2").arg(timeoutSeconds).arg(what).toStdString());
            }
            QTest::qWait(kPollMs);
        }
        checkEditDiagnostics();
        if (!dialogError_.isEmpty()) {
            const QString error = dialogError_;
            dialogError_.clear();
            throw std::runtime_error(error.toStdString());
        }
        return true;
    }

    int timeout(const char* key, int fallback) const {
        return scenario_.value(QStringLiteral("timeouts")).toObject().value(QString::fromLatin1(key)).toInt(fallback);
    }

    QAction* menuAction(QMenu* menu, const QString& objectName) const {
        for (QAction* action : menu ? menu->actions() : QList<QAction*>()) {
            if (action->objectName() == objectName) return action;
        }
        return nullptr;
    }

    void clickMenuAction(const QString& topLevelText, const QString& objectName) {
        QMenuBar* bar = window_->menus();
        QAction* top = nullptr;
        for (QAction* candidate : bar->actions()) {
            if (candidate->text().remove(QLatin1Char('&')) == topLevelText) {
                top = candidate;
                break;
            }
        }
        if (!top || !top->menu())
            throw std::runtime_error(QStringLiteral("Menu not found: %1").arg(topLevelText).toStdString());
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->actionGeometry(top).center());
        QMenu* menu = top->menu();
        waitFor(QStringLiteral("menu %1").arg(topLevelText), 10, [menu] { return menu->isVisible(); });
        QAction* action = menuAction(menu, objectName);
        if (!action)
            throw std::runtime_error(QStringLiteral("Menu action not found: %1").arg(objectName).toStdString());
        if (!action->isEnabled())
            throw std::runtime_error(QStringLiteral("Menu action is disabled: %1").arg(objectName).toStdString());
        const QRect rect = menu->actionGeometry(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, rect.center());
    }

    void clickButton(QAbstractButton* button, const QString& description) {
        if (!button) throw std::runtime_error(QStringLiteral("Button not found: %1").arg(description).toStdString());
        if (!button->isVisible() || !button->isEnabled()) {
            throw std::runtime_error(QStringLiteral("Button is not available: %1").arg(description).toStdString());
        }
        QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, button->rect().center());
    }

    // The non-native Qt dialog is deliberate: native Windows file dialogs are
    // outside this scenario's coverage. The path is typed into the real field.
    void chooseFileInNextDialog(const QString& path) {
        if (path.isEmpty()) throw std::runtime_error("The file-dialog path is empty");
        if (!nextFilePath_.isEmpty()) throw std::runtime_error("The file-dialog queue is already occupied");
        dialogError_.clear();
        nextFilePath_ = path;
    }

    sdk::TileMapModel* mapModel() const { return dynamic_cast<sdk::TileMapModel*>(window_->model_.get()); }

    void selectLevel() {
        const QJsonObject project = scenario_.value(QStringLiteral("project")).toObject();
        const QString kind = project.value(QStringLiteral("kind")).toString();
        const QString document = project.value(QStringLiteral("document")).toString();
        QPointer<QAbstractButton> kindButton =
            window_->findChild<QAbstractButton*>(QStringLiteral("kind.%1").arg(kind));
        waitFor(QStringLiteral("kind button %1").arg(kind), 30,
                [kindButton] { return kindButton && kindButton->isVisible() && kindButton->isEnabled(); });
        clickButton(kindButton, QStringLiteral("kind %1").arg(kind));
        waitFor(QStringLiteral("kind %1").arg(kind), 30, [this, kind] { return window_->kind_ == kind; });
        auto* list = window_->findChild<ui::DocumentList*>(QStringLiteral("documentList"));
        if (!list) throw std::runtime_error("The document list was not found");
        QModelIndex selected;
        for (int row = 0; row < list->model()->rowCount(); ++row) {
            const QModelIndex index = list->model()->index(row, 0);
            if (index.data(ui::DocumentRole::Id).toString() == document && !list->isRowHidden(row)) {
                selected = index;
                break;
            }
        }
        if (!selected.isValid()) {
            throw std::runtime_error(QStringLiteral("Document %1/%2 was not found").arg(kind, document).toStdString());
        }
        list->scrollTo(selected, QAbstractItemView::PositionAtCenter);
        QTest::qWait(0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualRect(selected).center());
        waitFor(QStringLiteral("editor for %1/%2").arg(kind, document), timeout("editor_s", 120),
                [this, document] { return window_->document_ == document && mapModel() && !window_->isBusy(); });
        if (mapModel()->type() != sdk::EditorType::TileMap) {
            throw std::runtime_error("The selected level did not open in the game's tile-map editor");
        }
    }

    QPoint canvasCellCenter(editors::MapCanvas* canvas, QPoint cell) const {
        const sdk::TileMapModel* map = mapModel();
        const QRect mapRect = canvas->mapRect();
        const int width = std::max(1, map->cellSize().width() * canvas->zoom() / 4);
        const int height = std::max(1, map->cellSize().height() * canvas->zoom() / 4);
        return QPoint(mapRect.left() + cell.x() * width + width / 2, mapRect.top() + cell.y() * height + height / 2);
    }

    QPoint editPoint(const QString& key) const {
        const QJsonObject point = scenario_.value(QStringLiteral("edit")).toObject().value(key).toObject();
        return QPoint(point.value(QStringLiteral("x")).toInt(-1), point.value(QStringLiteral("y")).toInt(-1));
    }

    QJsonArray objectSnapshot() const {
        QMap<QString, QJsonObject> sorted;
        for (const sdk::MapObject& object : mapModel()->objects()) {
            sorted.insert(object.id, QJsonObject{{QStringLiteral("id"), object.id},
                                                 {QStringLiteral("label"), object.label},
                                                 {QStringLiteral("x"), object.cells.x()},
                                                 {QStringLiteral("y"), object.cells.y()},
                                                 {QStringLiteral("width"), object.cells.width()},
                                                 {QStringLiteral("height"), object.cells.height()},
                                                 {QStringLiteral("movable"), object.movable},
                                                 {QStringLiteral("outlined"), object.outlined}});
        }
        QJsonArray result;
        for (const QJsonObject& object : sorted) result.append(object);
        return result;
    }

    QJsonObject budgetSnapshot() const {
        const sdk::Budget budget = mapModel()->budget();
        return {{QStringLiteral("used"), double(budget.used)},
                {QStringLiteral("capacity"), double(budget.capacity)},
                {QStringLiteral("text"), budget.text}};
    }

    void checkEditDiagnostics() const {
        if (!monitorEdit_) return;
        for (int i = editHostMessages_; i < window_->hostMessages_.size(); ++i) {
            const sdk::Diagnostic& diagnostic = window_->hostMessages_.at(i);
            if (diagnostic.severity != sdk::Severity::Info) throw std::runtime_error(diagnostic.message.toStdString());
        }
        const auto diagnostics = window_->session_->diagnostics();
        for (int i = editSessionMessages_; i < diagnostics.size(); ++i) {
            if (diagnostics.at(i).severity != sdk::Severity::Info)
                throw std::runtime_error(diagnostics.at(i).message.toStdString());
        }
    }

    void verifyMovedBlock() const {
        if (objectSnapshot() != expectedObjects_)
            throw std::runtime_error("The moved block or another map object differs from the exact expected state");
        if (budgetSnapshot() != originalBudget_)
            throw std::runtime_error("Moving the block changed the level or shared-pool byte budget");
    }

    void moveBlock() {
        const QJsonObject edit = scenario_.value(QStringLiteral("edit")).toObject();
        if (edit.value(QStringLiteral("operation")).toString() != QLatin1String("move-object-vertical"))
            throw std::runtime_error("Unsupported GUI edit operation");
        const QPoint from = editPoint(QStringLiteral("from"));
        const QPoint to = editPoint(QStringLiteral("to"));
        sdk::TileMapModel* map = mapModel();
        if (!map || from.x() != to.x() || std::abs(from.y() - to.y()) != 1 ||
            !QRect(QPoint(0, 0), map->gridSize()).contains(from) || !QRect(QPoint(0, 0), map->gridSize()).contains(to))
            throw std::runtime_error("The scenario must move vertically by exactly one valid cell");
        const QString label = edit.value(QStringLiteral("object_label")).toString();
        QString selectedId;
        int matches = 0;
        for (const sdk::MapObject& object : map->objects()) {
            if (object.cells.topLeft() == from && object.label == label &&
                object.id.startsWith(QLatin1String("object:")) && object.cells.size() == QSize(1, 1) &&
                object.movable) {
                selectedId = object.id;
                ++matches;
            }
            if (object.cells.contains(to)) throw std::runtime_error("The fixed destination cell is occupied");
        }
        if (matches != 1) throw std::runtime_error("The fixed source cell does not contain exactly one matching block");
        const QJsonArray original = objectSnapshot();
        expectedObjects_ = {};
        for (const QJsonValue& value : original) {
            QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("id")).toString() == selectedId) object.insert(QStringLiteral("y"), to.y());
            expectedObjects_.append(object);
        }
        originalBudget_ = budgetSnapshot();
        results_.insert(QStringLiteral("objects_before"), original);
        results_.insert(QStringLiteral("budget_before"), originalBudget_);
        auto* canvas = window_->findChild<editors::MapCanvas*>(QStringLiteral("mapCanvas"));
        auto* editor = dynamic_cast<editors::TileMapEditor*>(window_->editor_.get());
        if (!canvas || !editor) throw std::runtime_error("The real level canvas and editor were not found");
        if (editor->currentTool() != QLatin1String("select")) {
            QTest::keyClick(window_, Qt::Key_Escape);
            waitFor(QStringLiteral("Select tool"), 10,
                    [editor] { return editor->currentTool() == QLatin1String("select"); });
        }
        for (QWidget* parent = canvas->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto* area = qobject_cast<QScrollArea*>(parent)) {
                const QPoint center = canvasCellCenter(canvas, from);
                area->ensureVisible(center.x(), center.y(), 40, 60);
                break;
            }
        }
        QTest::qWait(0);
        const QPoint begin = canvasCellCenter(canvas, from);
        const QPoint end = canvasCellCenter(canvas, to);
        if (!canvas->visibleRegion().contains(begin) || !canvas->visibleRegion().contains(end))
            throw std::runtime_error("Both fixed drag endpoints must be visible");
        screenshot(QStringLiteral("before-edit"));
        editHostMessages_ = window_->hostMessages_.size();
        editSessionMessages_ = window_->session_->diagnostics().size();
        monitorEdit_ = true;
        QWindow* surface = window_->windowHandle();
        const QPoint press = canvas->mapTo(window_, begin);
        const QPoint release = canvas->mapTo(window_, end);
        QTest::mousePress(surface, Qt::LeftButton, Qt::NoModifier, press);
        QTest::mouseMove(surface, release, 25);
        QTest::mouseRelease(surface, Qt::LeftButton, Qt::NoModifier, release);
        waitFor(QStringLiteral("the fixed block moved from %1,%2 to %3,%4")
                    .arg(from.x())
                    .arg(from.y())
                    .arg(to.x())
                    .arg(to.y()),
                15, [this] { return objectSnapshot() == expectedObjects_; });
        monitorEdit_ = false;
        verifyMovedBlock();
        results_.insert(QStringLiteral("objects_after"), objectSnapshot());
        results_.insert(QStringLiteral("budget_after"), budgetSnapshot());
        results_.insert(QStringLiteral("only_fixed_block_moved"), true);
    }

    bool newError(int hostCount, int sessionCount) const {
        if (window_->hostMessages_.size() > hostCount) {
            for (int i = hostCount; i < window_->hostMessages_.size(); ++i) {
                if (window_->hostMessages_.at(i).severity == sdk::Severity::Error) return true;
            }
        }
        const QVector<sdk::Diagnostic> diagnostics =
            window_->session_ ? window_->session_->diagnostics() : QVector<sdk::Diagnostic>();
        for (int i = sessionCount; i < diagnostics.size(); ++i) {
            if (diagnostics.at(i).severity == sdk::Severity::Error) return true;
        }
        return false;
    }

    QJsonArray messages() const {
        QJsonArray result;
        auto append = [&result](const sdk::Diagnostic& diagnostic) {
            result.append(QJsonObject{{QStringLiteral("severity"), severityName(diagnostic.severity)},
                                      {QStringLiteral("source"), diagnostic.source},
                                      {QStringLiteral("message"), diagnostic.message},
                                      {QStringLiteral("location"), diagnostic.location},
                                      {QStringLiteral("kind"), diagnostic.kind},
                                      {QStringLiteral("document"), diagnostic.document}});
        };
        for (const sdk::Diagnostic& diagnostic : window_->hostMessages_) append(diagnostic);
        if (window_->session_) {
            for (const sdk::Diagnostic& diagnostic : window_->session_->diagnostics()) append(diagnostic);
        }
        return result;
    }

    void writeResults(const QString& status, const QString& reason) {
        results_.insert(QStringLiteral("status"), status);
        results_.insert(QStringLiteral("last_step"), lastStep_);
        results_.insert(QStringLiteral("stop_reason"), reason);
        results_.insert(QStringLiteral("events"), events_);
        results_.insert(QStringLiteral("screenshots"), screenshots_);
        results_.insert(QStringLiteral("studio_messages"), messages());
        results_.insert(QStringLiteral("seconds"), double(clock_.elapsed()) / 1000.0);
        if (auto* log = window_->findChild<QPlainTextEdit*>(QStringLiteral("buildOutput"))) {
            const QString text = log->toPlainText();
            results_.insert(QStringLiteral("build_log"), text);
            QFile file(output_.filePath(QStringLiteral("build.log")));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) file.write(text.toUtf8());
        }
        QSaveFile file(output_.filePath(QStringLiteral("result.json")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        file.write(QJsonDocument(results_).toJson(QJsonDocument::Indented));
        file.commit();
    }

    int finish(const QString& status, const QString& reason, int exitCode) {
        dialogTimer_->stop();
        if (status == QLatin1String("FAIL")) {
            try {
                screenshot(QStringLiteral("failure"));
            } catch (...) {
            }
        }
        lastStep_ = status == QLatin1String("FAIL") ? lastStep_ : QStringLiteral("complete");
        writeResults(status, reason);
        QApplication::exit(exitCode);
        return exitCode;
    }

    int run() {
        try {
            begin(QStringLiteral("show the real Studio window"));
            if (!QTest::qWaitForWindowExposed(window_, 10000))
                throw std::runtime_error("The Studio window was not exposed");

            begin(QStringLiteral("File > Open ROM and choose the hash-matched ROM"));
            chooseFileInNextDialog(scenario_.value(QStringLiteral("rom_path")).toString());
            clickMenuAction(QStringLiteral("File"), QStringLiteral("file.openRom"));

            begin(QStringLiteral("Choose Set up the source through the independent dialog timer"));
            waitFor(QStringLiteral("the source preparation choice"), timeout("open_s", 900),
                    [this] { return sourceChoiceHandled_; });

            begin(QStringLiteral("Wait for the prepared SMB1 JU project"));
            waitFor(QStringLiteral("the prepared project"), timeout("open_s", 900),
                    [this] { return window_->session_ && !window_->isBusy() && !QApplication::activeModalWidget(); });
            if (!window_->session_) throw std::runtime_error("No project session was opened");
            if (!consentHandled_) {
                note(QStringLiteral("dialog"),
                     QStringLiteral("No repository-script consent was requested while opening"));
            }
            const sdk::Profile profile = window_->session_->profile();
            const QJsonObject expectedProject = scenario_.value(QStringLiteral("project")).toObject();
            if (profile.name != expectedProject.value(QStringLiteral("name")).toString() ||
                profile.revision != expectedProject.value(QStringLiteral("revision")).toString() ||
                profile.platform != expectedProject.value(QStringLiteral("platform")).toString() ||
                profile.codec != expectedProject.value(QStringLiteral("codec")).toString()) {
                throw std::runtime_error(QStringLiteral("Wrong game project: %1 %2 %3 %4")
                                             .arg(profile.name, profile.revision, profile.platform, profile.codec)
                                             .toStdString());
            }
            projectPath_ = window_->projectPath_;
            results_.insert(QStringLiteral("project_path"), projectPath_);
            screenshot(QStringLiteral("project-ready"));

            begin(QStringLiteral("Open Levels > ground_6 in the game editor"));
            selectLevel();

            begin(QStringLiteral("Drag the fixed coin question block from 16,7 to 16,6"));
            moveBlock();

            begin(QStringLiteral("Verify the edit and unsaved-change indicator"));
            if (!window_->session_->isModified() || !window_->dirtyIndicator_->isVisible() ||
                !window_->saveButton_->isEnabled()) {
                throw std::runtime_error("The GUI did not show the unsaved edit");
            }
            screenshot(QStringLiteral("after-edit"));

            begin(QStringLiteral("Save through the toolbar button"));
            clickButton(window_->saveButton_, QStringLiteral("Save all"));
            waitFor(QStringLiteral("saving"), timeout("save_s", 30),
                    [this] { return !window_->session_->isModified() && !window_->saveButton_->isEnabled(); });
            screenshot(QStringLiteral("saved"));

            begin(QStringLiteral("Close the project through the File menu"));
            clickMenuAction(QStringLiteral("File"), QStringLiteral("file.closeProject"));
            waitFor(QStringLiteral("project close"), 30, [this] { return !window_->session_; });

            begin(QStringLiteral("Reopen the saved project through File > Open project"));
            chooseFileInNextDialog(projectPath_);
            clickMenuAction(QStringLiteral("File"), QStringLiteral("file.openProject"));
            waitFor(QStringLiteral("project reopen"), timeout("reopen_s", 120), [this] {
                return window_->session_ && window_->projectPath_ == projectPath_ && !window_->isBusy();
            });
            selectLevel();
            verifyMovedBlock();
            results_.insert(QStringLiteral("objects_reopened"), objectSnapshot());
            results_.insert(QStringLiteral("budget_reopened"), budgetSnapshot());
            if (window_->session_->isModified())
                throw std::runtime_error("The reopened project is unexpectedly modified");
            screenshot(QStringLiteral("reopened-edit"));

            begin(QStringLiteral("Build through the toolbar button and verify a fresh ROM"));
            const QString relative = scenario_.value(QStringLiteral("artifact")).toString();
            if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
                QDir::cleanPath(relative).startsWith(QLatin1String(".."))) {
                throw std::runtime_error("The scenario's artifact path is not a safe project-relative path");
            }
            const QString artifact = QDir(QFileInfo(projectPath_).absolutePath()).filePath(relative);
            const bool existed = QFileInfo::exists(artifact);
            results_.insert(QStringLiteral("previous_artifact_existed"), existed);
            const bool removed = existed && QFile::remove(artifact);
            results_.insert(QStringLiteral("previous_artifact_removed"), removed);
            if (existed && !removed) {
                throw std::runtime_error(
                    "The previous build artifact could not be "
                    "removed; refusing to accept a stale ROM");
            }
            const int hostErrors = window_->hostMessages_.size();
            const int sessionErrors = window_->session_->diagnostics().size();
            clickButton(window_->buildButton_, QStringLiteral("Build"));
            waitFor(QStringLiteral("the build"), timeout("build_s", 900), [this, hostErrors, sessionErrors] {
                const auto* kit = dynamic_cast<sdk::KitSession*>(window_->session_.get());
                return !window_->isBusy() &&
                       ((kit && !kit->lastBuild().isEmpty()) || newError(hostErrors, sessionErrors));
            });
            const auto* kit = dynamic_cast<sdk::KitSession*>(window_->session_.get());
            if (!kit || kit->lastBuild().isEmpty() || newError(hostErrors, sessionErrors)) {
                throw std::runtime_error("The build did not report a successfully built ROM");
            }
            const QFileInfo built(artifact);
            if (!built.isFile() || built.size() == 0) throw std::runtime_error("The build produced no fresh artifact");
            const QString actual = sha256File(artifact);
            if (actual.isEmpty()) throw std::runtime_error("The built ROM could not be hashed");
            results_.insert(QStringLiteral("artifact_path"), artifact);
            results_.insert(QStringLiteral("artifact_size"), double(built.size()));
            results_.insert(QStringLiteral("artifact_last_modified"), built.lastModified().toString(Qt::ISODate));
            results_.insert(QStringLiteral("actual_sha256"), actual);
            screenshot(QStringLiteral("built"));

            const QString expected = scenario_.value(QStringLiteral("expected_sha256")).toString();
            if (expected.isEmpty()) {
                results_.insert(QStringLiteral("baseline_candidate"),
                                QJsonObject{{QStringLiteral("sha256"), actual},
                                            {QStringLiteral("before_screenshot"), QStringLiteral("before-edit")},
                                            {QStringLiteral("after_screenshot"), QStringLiteral("after-edit")},
                                            {QStringLiteral("change"), scenario_.value(QStringLiteral("edit"))}});
                return finish(QStringLiteral("BASELINE_PENDING"),
                              QStringLiteral("The full GUI path completed, but no "
                                             "expected SHA-256 has been approved."),
                              3);
            }
            if (actual != expected) {
                return finish(QStringLiteral("FAIL"),
                              QStringLiteral("Built SHA-256 %1 does not match approved %2").arg(actual, expected), 1);
            }
            return finish(QStringLiteral("PASS"), QStringLiteral("The fresh built ROM matches the approved SHA-256."),
                          0);
        } catch (const std::exception& error) {
            return finish(QStringLiteral("FAIL"), QString::fromUtf8(error.what()), 1);
        } catch (...) {
            return finish(QStringLiteral("FAIL"), QStringLiteral("Unknown GUI regression failure"), 1);
        }
    }

    MainWindow* window_;
    QJsonObject scenario_;
    QDir output_;
    QJsonObject results_;
    QJsonArray events_;
    QJsonArray screenshots_;
    QElapsedTimer clock_;
    QString lastStep_ = QStringLiteral("initialization");
    QString dialogError_;
    QTimer* dialogTimer_ = nullptr;
    QPointer<QWidget> handledModal_;
    QString nextFilePath_;
    bool sourceChoiceHandled_ = false;
    bool consentHandled_ = false;
    QString projectPath_;
    QJsonArray expectedObjects_;
    QJsonObject originalBudget_;
    bool monitorEdit_ = false;
    int editHostMessages_ = 0;
    int editSessionMessages_ = 0;
    int shot_ = 0;
};

}  // namespace rl::shell

int main(int argc, char* argv[]) {
    if (qEnvironmentVariable("RETROLARIUM_STUDIO_HOME").isEmpty()) {
        qCritical(
            "RETROLARIUM_STUDIO_HOME is required: the GUI regression refuses "
            "to touch user settings");
        return 2;
    }
    rl::frame::restrictDllSearch(true);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    Q_INIT_RESOURCE(studio);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Retrolarium Studio"));
    QApplication::setOrganizationName(QStringLiteral("Retrolarium"));
    QApplication::setApplicationVersion(QStringLiteral(RETROLARIUM_STUDIO_VERSION));

    const QString home = qEnvironmentVariable("RETROLARIUM_STUDIO_HOME");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(home).filePath(QStringLiteral("settings")));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().setValue(QStringLiteral("ui/language"), QStringLiteral("en"));
    const QString language = rl::shell::SettingsDialog::language();
    rl::theme::setInterfaceLanguage(language);
    QTranslator studioTranslator;
    QTranslator qtTranslator;
    if (studioTranslator.load(QStringLiteral(":/i18n/retrolarium_studio_%1").arg(language))) {
        QApplication::installTranslator(&studioTranslator);
        if (language != QLatin1String("en") && qtTranslator.load(QStringLiteral(":/i18n/qtbase_%1").arg(language))) {
            QApplication::installTranslator(&qtTranslator);
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Retrolarium Studio private GUI regression"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"), QStringLiteral("Effective scenario JSON"),
                                            QStringLiteral("path"));
    parser.addOption(scenarioOption);
    parser.process(app);
    const QString scenarioPath = parser.value(scenarioOption);
    QFile scenarioFile(scenarioPath);
    if (scenarioPath.isEmpty() || !scenarioFile.open(QIODevice::ReadOnly)) {
        qCritical("A readable --scenario JSON is required");
        return 2;
    }
    const QJsonObject scenario = QJsonDocument::fromJson(scenarioFile.readAll()).object();
    if (scenario.isEmpty() || scenario.value(QStringLiteral("output")).toString().isEmpty()) {
        qCritical("The scenario is empty or has no output directory");
        return 2;
    }

    rl::plugins::registerBuiltinPlugins();
    rl::theme::Theme::instance().install(app, rl::theme::Mode::Dark);
    QApplication::setWindowIcon(rl::theme::brand::appIcon(rl::theme::Theme::instance().mode()));
    rl::shell::MainWindow window;
    window.resize(1600, 1000);
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect room = screen->availableGeometry();
        window.move(room.center() - window.rect().center());
    }
    window.show();
    rl::shell::GuiRegression test(&window, scenario);
    test.start();
    return app.exec();
}
