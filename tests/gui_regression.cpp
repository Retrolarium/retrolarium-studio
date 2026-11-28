#include "library_layout_checks.hpp"
#include "builtin_plugins.hpp"
#include "editors/library_sheet.hpp"
#include "editors/tilemap_editor.hpp"
#include "editors/tile_editor.hpp"
#include "editors/composition_editor.hpp"
#include "editors/sequencer_editor.hpp"
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
#include <QComboBox>
#include <QTableView>
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
#include <QSet>
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
        window_->installEventFilter(this);
        for (auto* action : window_->findChildren<QAction*>()) {
            connect(action, &QAction::triggered, this, [this, action] {
                note(QStringLiteral("menu-activated"), action->objectName() + QLatin1String(" | ") + action->text());
            });
        }
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            if (!finished_) {
                note(QStringLiteral("unexpected-quit"),
                     QStringLiteral("Application quit before the scenario finished"));
                writeResults(QStringLiteral("FAIL"), QStringLiteral("Application quit before the scenario finished"));
            }
        });
    }

    int exitCode() const { return exitCode_; }

    void start() {
        // The timer also runs inside a modal dialog's nested exec() loop.
        dialogTimer_->start();
        QTimer::singleShot(0, this, [this] { run(); });
    }

private:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == window_ && event->type() == QEvent::Close) {
            note(QStringLiteral("window-close"), QStringLiteral("MainWindow received a close event"));
            writeResults(QStringLiteral("RUNNING"), QString());
        }
        return QObject::eventFilter(watched, event);
    }

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
        const sdk::Library inspector = window_->model_->inspect().library;
        const sdk::Library sheetLibrary = window_->model_->sheet();
        QJsonArray checks = results_.value(QStringLiteral("library_layout_checks")).toArray();
        if (!inspector.grid && !inspector.items.isEmpty()) {
            QStringList ids;
            for (const sdk::LibraryItem& item : inspector.items) ids.append(item.id);
            const auto layout = rl::tests::inspectLibraryLayout(window_, ids);
            checks.append(QJsonObject{{QStringLiteral("stage"), stage},
                                      {QStringLiteral("kind"), QStringLiteral("cards")},
                                      {QStringLiteral("card_count"), layout.cards.size()},
                                      {QStringLiteral("overlap_free"), layout.error.isEmpty()},
                                      {QStringLiteral("error"), layout.error},
                                      {QStringLiteral("cards"), layout.cards}});
            results_.insert(QStringLiteral("library_layout_checks"), checks);
            if (!layout.error.isEmpty()) throw std::runtime_error(layout.error.toStdString());
        }
        QSet<QString> expected, found;
        if (inspector.grid && !inspector.items.isEmpty()) expected.insert(QStringLiteral("inspector"));
        if (!sheetLibrary.items.isEmpty()) expected.insert(QStringLiteral("sheet"));
        for (auto* widget : window_->findChildren<QWidget*>(QStringLiteral("librarySheet"))) {
            auto* sheet = dynamic_cast<editors::LibrarySheet*>(widget);
            if (!sheet || !sheet->isVisibleTo(window_)) continue;
            const QString source = sheet->property("librarySource").toString();
            if (!expected.contains(source) || found.contains(source))
                throw std::runtime_error("Unexpected or duplicate visible library grid");
            found.insert(source);
            const auto layout =
                rl::tests::inspectLibrarySheet(sheet, source == QLatin1String("sheet") ? sheetLibrary : inspector);
            checks.append(QJsonObject{
                {QStringLiteral("stage"), stage},
                {QStringLiteral("kind"), QStringLiteral("grid")},
                {QStringLiteral("source"), source},
                {QStringLiteral("card_count"), layout.cards.size()},
                {QStringLiteral("overlap_free"), layout.error.isEmpty()},
                {QStringLiteral("error"), layout.error},
                {QStringLiteral("geometry_sha256"), stateHash(QJsonObject{{QStringLiteral("cards"), layout.cards}})}});
            results_.insert(QStringLiteral("library_layout_checks"), checks);
            if (!layout.error.isEmpty()) throw std::runtime_error(layout.error.toStdString());
        }
        if (expected != found) throw std::runtime_error("Expected library grid is missing from the visible GUI");
        results_.insert(QStringLiteral("library_layout_contract"), 2);
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
        if (rect.isEmpty() || menu->actionAt(rect.center()) != action)
            throw std::runtime_error("The menu action has no valid visible hit target");
        // Navigate the visible popup with real key events. Synthetic mouse
        // motion is not delivered consistently by every desktop backend.
        QTest::keyClick(menu, Qt::Key_Home);
        for (int i = 0; menu->activeAction() != action && i < menu->actions().size(); ++i)
            QTest::keyClick(menu, Qt::Key_Down);
        if (menu->activeAction() != action)
            throw std::runtime_error("The requested menu item could not be highlighted through key events");
        note(QStringLiteral("menu-enter"), objectName);
        QTest::keyClick(menu, Qt::Key_Return);
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

    void selectDocument(const QString& kind, const QString& document) {
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
                [this, document] { return window_->document_ == document && window_->model_ && !window_->isBusy(); });
        note(QStringLiteral("document"), kind + QLatin1Char('/') + document);
    }

    void inspectDocuments() {
        QJsonArray inventory;
        const auto kinds = window_->session_->kinds();
        for (const auto& kind : kinds) {
            if (kind.id == QLatin1String("demo") || kind.id == QLatin1String("playtest")) continue;
            begin(QStringLiteral("Inspect the real %1 tab").arg(kind.id));
            QJsonArray documents;
            QString first;
            for (const auto& group : window_->session_->documents(kind.id)) {
                for (const auto& document : group.documents) {
                    if (first.isEmpty()) first = document.id;
                    documents.append(QJsonObject{{QStringLiteral("id"), document.id},
                                                 {QStringLiteral("label"), document.label},
                                                 {QStringLiteral("group"), group.title},
                                                 {QStringLiteral("meta"), document.meta}});
                }
            }
            QJsonObject item{{QStringLiteral("kind"), kind.id}, {QStringLiteral("documents"), documents}};
            if (first.isEmpty()) throw std::runtime_error("An available tab has no documents");
            QJsonArray selected =
                scenario_.value(QStringLiteral("inspect_documents")).toObject().value(kind.id).toArray();
            if (selected.isEmpty()) selected.append(first);
            QJsonArray samples;
            for (const auto& value : selected) {
                const QString document = value.toString();
                selectDocument(kind.id, document);
                const auto* model = window_->model_.get();
                QJsonArray fields, tools, library, frames;
                for (const auto& field : model->inspect().fields) {
                    QJsonArray choices;
                    for (const auto& choice : field.choices) choices.append(choice);
                    fields.append(QJsonObject{{QStringLiteral("key"), field.key},
                                              {QStringLiteral("label"), field.label},
                                              {QStringLiteral("type"), int(field.type)},
                                              {QStringLiteral("value"), QJsonValue::fromVariant(field.value)},
                                              {QStringLiteral("minimum"), field.minimum},
                                              {QStringLiteral("maximum"), field.maximum},
                                              {QStringLiteral("choices"), choices}});
                }
                if (auto* map = dynamic_cast<const sdk::TileMapModel*>(model))
                    for (const auto& tool : map->tools()) tools.append(tool.id);
                auto sheet = model->sheet();
                if (sheet.items.isEmpty()) sheet = model->inspect().library;
                for (const auto& entry : sheet.items)
                    library.append(
                        QJsonObject{{QStringLiteral("id"), entry.id}, {QStringLiteral("label"), entry.label}});
                if (auto* composition = dynamic_cast<const sdk::CompositionModel*>(model))
                    for (const auto& frame : composition->frames()) frames.append(frame.label);
                samples.append(QJsonObject{{QStringLiteral("document"), document},
                                           {QStringLiteral("editor"), sdk::editorName(model->type())},
                                           {QStringLiteral("state"), semanticState()},
                                           {QStringLiteral("fields"), fields},
                                           {QStringLiteral("tools"), tools},
                                           {QStringLiteral("library"), library},
                                           {QStringLiteral("frames"), frames},
                                           {QStringLiteral("read_only"), model->isReadOnly()},
                                           {QStringLiteral("read_only_reason"), model->readOnlyReason()}});
                screenshot(QStringLiteral("inspect-%1").arg(kind.id));
            }
            item.insert(QStringLiteral("samples"), samples);
            inventory.append(item);
            results_.insert(QStringLiteral("inventory"), inventory);
        }
        if (window_->session_->isModified()) throw std::runtime_error("Read-only GUI inspection changed the project");
    }

    void selectScenarioDocument() {
        const auto project = scenario_.value(QStringLiteral("project")).toObject();
        selectDocument(project.value(QStringLiteral("kind")).toString(),
                       project.value(QStringLiteral("document")).toString());
        const QString expected = project.value(QStringLiteral("editor")).toString();
        if (!expected.isEmpty() && sdk::editorName(window_->model_->type()) != expected)
            throw std::runtime_error("The pinned document opened in the wrong editor");
    }

    QString operation() const {
        return scenario_.value(QStringLiteral("edit")).toObject().value(QStringLiteral("operation")).toString();
    }

    QWidget* visibleWidget(const QString& name) {
        QPointer<QWidget> result;
        waitFor(QStringLiteral("visible widget %1").arg(name), 15, [this, &result, name] {
            int count = 0;
            for (QWidget* widget : window_->findChildren<QWidget*>(name)) {
                if (widget->isVisibleTo(window_) && widget->isEnabled()) {
                    result = widget;
                    ++count;
                }
            }
            if (count > 1) throw std::runtime_error(QStringLiteral("Ambiguous widget: %1").arg(name).toStdString());
            return count == 1;
        });
        return result;
    }

    void revealPoint(QWidget* widget, const QPoint& point) {
        for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
                const QPoint at = widget->mapTo(scroll->widget(), point);
                scroll->ensureVisible(at.x(), at.y(), 24, 24);
                break;
            }
        }
        waitFor(QStringLiteral("unclipped input point in %1").arg(widget->objectName()), 10,
                [widget, point] { return widget->visibleRegion().contains(point); });
    }

    void clickNamed(const QString& name) {
        QWidget* widget = visibleWidget(name);
        revealPoint(widget, widget->rect().center());
        QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, widget->rect().center());
    }

    QJsonObject semanticState() const {
        QJsonObject state;
        auto* model = window_->model_.get();
        if (auto* table = dynamic_cast<sdk::TableModel*>(model)) {
            QJsonArray cells;
            QJsonArray columns;
            for (const auto& column : table->columns()) columns.append(column.key);
            for (int r = 0; r < table->rowCount(); ++r) {
                QJsonArray row;
                for (int c = 0; c < columns.size(); ++c) row.append(table->cell(r, c));
                cells.append(row);
            }
            state.insert(QStringLiteral("columns"), columns);
            state.insert(QStringLiteral("cells"), cells);
        } else if (auto* tiles = dynamic_cast<sdk::TileModel*>(model)) {
            QJsonArray all;
            for (int t = 0; t < tiles->tileCount(); ++t) {
                QJsonArray pixels;
                for (int value : tiles->tilePixels(t)) pixels.append(value);
                all.append(pixels);
            }
            state.insert(QStringLiteral("tiles"), all);
        } else if (auto* palette = dynamic_cast<sdk::PaletteModel*>(model)) {
            QJsonArray rows;
            for (const auto& row : palette->rows()) {
                QJsonArray values, colors;
                for (const QString& label : row.labels) values.append(label);
                for (const QColor& color : row.colors) colors.append(color.name(QColor::HexArgb));
                rows.append(QJsonObject{{QStringLiteral("label"), row.label},
                                        {QStringLiteral("values"), values},
                                        {QStringLiteral("colors"), colors}});
            }
            state.insert(QStringLiteral("rows"), rows);
        } else if (auto* map = dynamic_cast<sdk::TileMapModel*>(model)) {
            QJsonArray cells;
            for (int y = 0; y < map->gridSize().height(); ++y) {
                QJsonArray row;
                for (int x = 0; x < map->gridSize().width(); ++x) row.append(map->cellValue(QPoint(x, y)));
                cells.append(row);
            }
            state.insert(QStringLiteral("cells"), cells);
            state.insert(QStringLiteral("objects"), objectSnapshot());
        } else if (auto* composition = dynamic_cast<sdk::CompositionModel*>(model)) {
            QJsonArray pieces;
            for (const auto& piece : composition->pieces())
                pieces.append(QJsonObject{{QStringLiteral("id"), piece.id},
                                          {QStringLiteral("label"), piece.label},
                                          {QStringLiteral("x"), piece.pixels.x()},
                                          {QStringLiteral("y"), piece.pixels.y()},
                                          {QStringLiteral("width"), piece.pixels.width()},
                                          {QStringLiteral("height"), piece.pixels.height()}});
            state.insert(QStringLiteral("frame"), composition->currentFrame());
            state.insert(QStringLiteral("pieces"), pieces);
        } else if (auto* sequencer = dynamic_cast<sdk::SequencerModel*>(model)) {
            QJsonArray channels;
            const auto list = sequencer->channels();
            for (int c = 0; c < list.size(); ++c) {
                QJsonArray notes;
                for (const auto& note : sequencer->notes(c))
                    notes.append(QJsonObject{{QStringLiteral("id"), note.id},
                                             {QStringLiteral("start"), note.start},
                                             {QStringLiteral("length"), note.length},
                                             {QStringLiteral("pitch"), note.pitch},
                                             {QStringLiteral("pitch_editable"), note.pitchEditable}});
                channels.append(QJsonObject{{QStringLiteral("id"), list.at(c).id}, {QStringLiteral("notes"), notes}});
            }
            state.insert(QStringLiteral("channels"), channels);
        } else throw std::runtime_error("No semantic snapshot for this editor type");
        if (operation() == QLatin1String("inspector-field") || operation() == QLatin1String("composition-inspector")) {
            QJsonObject fields;
            for (const auto& field : model->inspect().fields)
                fields.insert(field.key, QJsonValue::fromVariant(field.value));
            state.insert(QStringLiteral("fields"), fields);
        }
        if (operation() == QLatin1String("palette-channel")) {
            auto* palette = dynamic_cast<sdk::PaletteModel*>(model);
            if (!palette) throw std::runtime_error("Expected a channel palette editor");
            QJsonArray channels;
            for (int value : palette->currentChannels()) channels.append(value);
            state.insert(QStringLiteral("channels"), channels);
        }
        return state;
    }

    static QJsonValue patched(QJsonValue value, const QStringList& path, int index, const QJsonValue& replacement) {
        if (index == path.size()) return replacement;
        if (value.isArray()) {
            QJsonArray array = value.toArray();
            bool ok = false;
            const int at = path.at(index).toInt(&ok);
            if (!ok || at < 0 || at >= array.size()) throw std::runtime_error("Invalid snapshot array address");
            array.replace(at, patched(array.at(at), path, index + 1, replacement));
            return array;
        }
        QJsonObject object = value.toObject();
        if (!object.contains(path.at(index))) throw std::runtime_error("Invalid snapshot field address");
        object.insert(path.at(index), patched(object.value(path.at(index)), path, index + 1, replacement));
        return object;
    }

    static QString stateHash(const QJsonObject& state) {
        return QString::fromLatin1(
            QCryptographicHash::hash(QJsonDocument(state).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
                .toHex());
    }

    void chooseFrame(int frame) {
        auto* model = dynamic_cast<sdk::CompositionModel*>(window_->model_.get());
        if (!model || frame < 0 || frame >= model->frames().size())
            throw std::runtime_error("Missing pinned animation frame");
        clickNamed(QStringLiteral("composition.frame.%1").arg(frame));
        waitFor(QStringLiteral("animation frame %1").arg(frame), 10,
                [model, frame] { return model->currentFrame() == frame; });
    }

    void clickSegment(const QString& control, const QString& id) {
        auto* bar = visibleWidget(control);
        auto* button = bar->findChild<QToolButton*>(QStringLiteral("segment.%1").arg(id));
        if (!button || !button->isVisibleTo(window_) || !button->isEnabled())
            throw std::runtime_error("Missing or unavailable pinned toolbar segment");
        QTest::mouseClick(button, Qt::LeftButton);
    }

    void waitForSelectionPresentation() {
        // SelectionChanged schedules normal inspector/editor refreshes for the
        // next GUI event pass. Capture the selected target, not the old header.
        QTest::qWait(0);
        waitFor(QStringLiteral("the visible inspector matches the selected target"), 10, [this] {
            const QString expected = window_->model_->inspect().title;
            for (auto* title : window_->findChildren<QLabel*>(QStringLiteral("inspector.title")))
                if (title->isVisibleTo(window_) && title->text() == expected) return true;
            return false;
        });
        if (auto* palette = dynamic_cast<sdk::PaletteModel*>(window_->model_.get())) {
            const QPoint entry = palette->currentEntry();
            const QString name = QStringLiteral("palette.entry.%1.%2").arg(entry.y()).arg(entry.x());
            waitFor(QStringLiteral("the selected palette swatch is visibly painted with its current color"), 10,
                    [this, palette, entry, name] {
                        const auto rows = palette->rows();
                        const QColor expected = rows.at(entry.y()).colors.at(entry.x());
                        for (auto* widget : window_->findChildren<QWidget*>(name)) {
                            if (!widget->isVisibleTo(window_) || widget->visibleRegion().isEmpty() ||
                                widget->width() < 20 || widget->height() < 20)
                                continue;
                            const QImage frame = widget->grab().toImage();
                            const qreal ratio = frame.devicePixelRatio();
                            const QColor body = frame.pixelColor(qRound(8 * ratio), qRound(8 * ratio));
                            const QColor selection = frame.pixelColor(0, 0);
                            if (body == expected && selection == rl::theme::current().accent) return true;
                        }
                        return false;
                    });
            results_.insert(QStringLiteral("palette_presentation_verified"), true);
        }
    }

    void prepareEdit() {
        const QJsonObject edit = scenario_.value(QStringLiteral("edit")).toObject();
        const QString op = operation();
        if (op == QLatin1String("inspector-field") && edit.contains(QStringLiteral("object"))) {
            auto* map = mapModel();
            auto* canvas = qobject_cast<editors::MapCanvas*>(visibleWidget(QStringLiteral("mapCanvas")));
            const QString id = edit.value(QStringLiteral("object")).toString();
            clickSegment(QStringLiteral("mapTools"), QStringLiteral("select"));
            bool found = false;
            for (const auto& object : map->objects()) {
                if (object.id != id) continue;
                const QPoint point = canvasCellCenter(canvas, object.cells.center());
                revealPoint(canvas, point);
                QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
                found = true;
                break;
            }
            if (!found) throw std::runtime_error("Missing pinned map object for inspector editing");
            waitFor(QStringLiteral("pinned map object"), 10, [map, id] { return map->selectedObject() == id; });
        } else if (op == QLatin1String("inspector-field") && edit.contains(QStringLiteral("frame"))) {
            auto* model = dynamic_cast<sdk::CompositionModel*>(window_->model_.get());
            if (!model) throw std::runtime_error("Expected a framed document inspector");
            const int frame = edit.value(QStringLiteral("frame")).toInt(-1);
            if (model->frames().size() > 1) chooseFrame((frame + 1) % model->frames().size());
            chooseFrame(frame);
        } else if (op == QLatin1String("map-toolbar-cell")) {
            auto* map = mapModel();
            const QString layer = edit.value(QStringLiteral("layer")).toString();
            if (map->currentLayer() != layer) clickSegment(QStringLiteral("mapLayers"), layer);
            waitFor(QStringLiteral("pinned map layer"), 10, [map, layer] { return map->currentLayer() == layer; });
        } else if (op == QLatin1String("tile-pixel")) {
            auto* model = dynamic_cast<sdk::TileModel*>(window_->model_.get());
            auto* atlas = qobject_cast<editors::TileAtlas*>(visibleWidget(QStringLiteral("tileAtlas")));
            if (!model || !atlas || model->tileSize() != QSize(8, 8))
                throw std::runtime_error("Expected an 8x8 tile editor");
            const int tile = edit.value(QStringLiteral("tile")).toInt(-1);
            const QPoint point((tile % 16) * editors::TileAtlas::kCell + 17,
                               (tile / 16) * atlas->cellHeight() + atlas->cellHeight() / 2);
            revealPoint(atlas, point);
            QTest::mouseClick(atlas, Qt::LeftButton, Qt::NoModifier, point);
            waitFor(QStringLiteral("pinned tile"), 10, [model, tile] { return model->currentTile() == tile; });
        } else if (op == QLatin1String("palette-color") || op == QLatin1String("palette-channel")) {
            auto* model = dynamic_cast<sdk::PaletteModel*>(window_->model_.get());
            const int row = edit.value(QStringLiteral("row")).toInt(-1),
                      entry = edit.value(QStringLiteral("entry")).toInt(-1);
            if (!model) throw std::runtime_error("Expected palette editor");
            clickNamed(QStringLiteral("palette.entry.%1.%2").arg(row).arg(entry));
            waitFor(QStringLiteral("palette entry"), 10,
                    [model, row, entry] { return model->currentEntry() == QPoint(entry, row); });
        } else if (op == QLatin1String("composition-field") || op == QLatin1String("composition-inspector")) {
            auto* model = dynamic_cast<sdk::CompositionModel*>(window_->model_.get());
            if (!model) throw std::runtime_error("Expected animation editor");
            const int frame = edit.value(QStringLiteral("frame")).toInt(-1);
            if (model->frames().isEmpty()) {
                if (frame != -1) throw std::runtime_error("An unframed composition must declare frame -1");
            } else {
                if (model->frames().size() > 1) chooseFrame((frame + 1) % model->frames().size());
                chooseFrame(frame);
            }
            const QString pieceId = edit.value(QStringLiteral("piece")).toString();
            auto* canvas =
                qobject_cast<editors::CompositionCanvas*>(visibleWidget(QStringLiteral("compositionCanvas")));
            bool found = false;
            for (const auto& piece : model->pieces()) {
                if (piece.id != pieceId) continue;
                const QRect image = canvas->imageRect();
                const qreal zoom = canvas->displayZoom();
                const QRect focus = model->focusRect();
                const QPoint point = image.topLeft() + QPoint(qRound((piece.pixels.center().x() - focus.x()) * zoom),
                                                              qRound((piece.pixels.center().y() - focus.y()) * zoom));
                revealPoint(canvas, point);
                QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
                found = true;
                break;
            }
            if (!found) throw std::runtime_error("Missing pinned sprite piece");
            waitFor(QStringLiteral("sprite piece"), 10, [model, pieceId] { return model->selectedPiece() == pieceId; });
        } else if (op == QLatin1String("sequence-note")) {
            auto* model = dynamic_cast<sdk::SequencerModel*>(window_->model_.get());
            if (!model) throw std::runtime_error("Expected music editor");
            const QString channel = edit.value(QStringLiteral("channel")).toString();
            const auto channels = model->channels();
            if (channels.size() > 1)
                clickNamed(QStringLiteral("sequencer.channel.%1")
                               .arg(channels.at((model->currentChannel() + 1) % channels.size()).id));
            clickNamed(QStringLiteral("sequencer.channel.%1").arg(channel));
            waitFor(QStringLiteral("music channel"), 10,
                    [model, channel] { return model->channels().at(model->currentChannel()).id == channel; });
            auto* roll = qobject_cast<editors::PianoRoll*>(visibleWidget(QStringLiteral("pianoRoll")));
            const QString id = edit.value(QStringLiteral("note")).toString();
            bool found = false;
            for (const auto& note : model->notes(model->currentChannel())) {
                if (note.id != id) continue;
                if (!note.pitchEditable || note.pitch < 0)
                    throw std::runtime_error("The pinned note cannot change pitch");
                const QPoint point(editors::PianoRoll::kKeyWidth + note.start * roll->pixelsPerTick() +
                                       std::max(1, note.length * roll->pixelsPerTick() / 2),
                                   (model->pitchMaximum() - note.pitch) * editors::PianoRoll::kRowHeight + 5);
                revealPoint(roll, point);
                QTest::mouseClick(roll, Qt::LeftButton, Qt::NoModifier, point);
                found = true;
                break;
            }
            if (!found) throw std::runtime_error("Missing pinned music note");
            waitFor(QStringLiteral("music note"), 10, [model, id] { return model->selectedNote() == id; });
        }
        waitForSelectionPresentation();
    }

    void editField(const QString& key, const QString& value) {
        QWidget* widget = visibleWidget(QStringLiteral("inspector.field.%1").arg(key));
        revealPoint(widget, widget->rect().center());
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            const int index = combo->findData(value);
            if (index < 0) throw std::runtime_error("Missing pinned field choice");
            QTest::mouseClick(combo, Qt::LeftButton, Qt::NoModifier, combo->rect().center());
            // Send navigation to the open popup, as a user does. Sending it
            // directly to QComboBox activates every intermediate value and
            // creates multiple edit commands before the final choice.
            auto* popup = combo->view();
            waitFor(QStringLiteral("the inspector choice popup opens"), 10, [popup] { return popup->isVisible(); });
            QTest::keyClick(popup, Qt::Key_Home);
            for (int i = 0; i < index; ++i) QTest::keyClick(popup, Qt::Key_Down);
            QTest::keyClick(popup, Qt::Key_Return);
        } else if (auto* line = qobject_cast<QLineEdit*>(widget)) {
            if (line->isReadOnly()) throw std::runtime_error("The pinned field is read-only");
            QTest::mouseClick(line, Qt::LeftButton, Qt::NoModifier, line->rect().center());
            QTest::keyClick(line, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(line, value);
            QTest::keyClick(line, Qt::Key_Return);
        } else throw std::runtime_error("Unsupported inspector control");
    }

    void applyTabEdit() {
        results_.insert(QStringLiteral("initial_editor_state"), semanticState());
        prepareEdit();
        const QJsonObject before = semanticState();
        expectedState_ = before;
        results_.insert(QStringLiteral("semantic_before"), before);
        const QJsonObject edit = scenario_.value(QStringLiteral("edit")).toObject();
        const QString op = operation();
        const QJsonValue desired = edit.value(QStringLiteral("new"));
        QStringList path;
        if (op == QLatin1String("table-cell")) {
            path = QStringList{QStringLiteral("cells"), QString::number(edit.value(QStringLiteral("row")).toInt()),
                               QString::number(edit.value(QStringLiteral("column")).toInt())};
        } else if (op == QLatin1String("tile-pixel")) {
            path = QStringList{
                QStringLiteral("tiles"), QString::number(edit.value(QStringLiteral("tile")).toInt()),
                QString::number(edit.value(QStringLiteral("y")).toInt() * 8 + edit.value(QStringLiteral("x")).toInt())};
        } else if (op == QLatin1String("map-brush-cell") || op == QLatin1String("map-toolbar-cell")) {
            path = QStringList{QStringLiteral("cells"), QString::number(edit.value(QStringLiteral("y")).toInt()),
                               QString::number(edit.value(QStringLiteral("x")).toInt())};
        } else if (op == QLatin1String("inspector-field") || op == QLatin1String("composition-inspector")) {
            path = QStringList{QStringLiteral("fields"), edit.value(QStringLiteral("field")).toString()};
        } else if (op == QLatin1String("palette-channel")) {
            path =
                QStringList{QStringLiteral("channels"), QString::number(edit.value(QStringLiteral("channel")).toInt())};
        } else if (op == QLatin1String("palette-color")) {
            path = QStringList{QStringLiteral("rows"), QString::number(edit.value(QStringLiteral("row")).toInt()),
                               QStringLiteral("values"), QString::number(edit.value(QStringLiteral("entry")).toInt())};
        } else if (op == QLatin1String("composition-field")) {
            const auto pieces = before.value(QStringLiteral("pieces")).toArray();
            for (int i = 0; i < pieces.size(); ++i)
                if (pieces.at(i).toObject().value(QStringLiteral("id")) == edit.value(QStringLiteral("piece")))
                    path = QStringList{QStringLiteral("pieces"), QString::number(i), QStringLiteral("label")};
        } else if (op == QLatin1String("sequence-note")) {
            const auto channels = before.value(QStringLiteral("channels")).toArray();
            for (int c = 0; c < channels.size(); ++c) {
                const auto notes = channels.at(c).toObject().value(QStringLiteral("notes")).toArray();
                for (int n = 0; n < notes.size(); ++n)
                    if (notes.at(n).toObject().value(QStringLiteral("id")) == edit.value(QStringLiteral("note")))
                        path = QStringList{QStringLiteral("channels"), QString::number(c), QStringLiteral("notes"),
                                           QString::number(n), QStringLiteral("pitch")};
            }
        }
        if (path.isEmpty()) throw std::runtime_error("Unsupported or missing pinned edit target");
        QJsonValue oldValue = before;
        for (const auto& key : path)
            oldValue = oldValue.isArray() ? oldValue.toArray().at(key.toInt()) : oldValue.toObject().value(key);
        if (oldValue != edit.value(QStringLiteral("old")))
            throw std::runtime_error(
                QStringLiteral("Pinned initial value differs: got %1")
                    .arg(QString::fromUtf8(QJsonDocument(QJsonArray{oldValue}).toJson(QJsonDocument::Compact)))
                    .toStdString());
        if (oldValue == desired) throw std::runtime_error("The scenario edit must change ROM content");
        expectedState_ = patched(before, path, 0, desired).toObject();
        if (op == QLatin1String("palette-color")) {
            const int color = edit.value(QStringLiteral("gamut")).toInt(-1);
            auto* palette = dynamic_cast<sdk::PaletteModel*>(window_->model_.get());
            expectedState_ =
                patched(expectedState_,
                        {QStringLiteral("rows"), QString::number(edit.value(QStringLiteral("row")).toInt()),
                         QStringLiteral("colors"), QString::number(edit.value(QStringLiteral("entry")).toInt())},
                        0, palette->gamut().at(color).name(QColor::HexArgb))
                    .toObject();
        }
        for (const auto& value : edit.value(QStringLiteral("object_labels")).toArray()) {
            const QJsonObject change = value.toObject();
            const QJsonArray objects = before.value(QStringLiteral("objects")).toArray();
            int found = 0;
            for (int i = 0; i < objects.size(); ++i) {
                const auto object = objects.at(i).toObject();
                if (object.value(QStringLiteral("id")) != change.value(QStringLiteral("id"))) continue;
                if (object.value(QStringLiteral("label")) != change.value(QStringLiteral("old")))
                    throw std::runtime_error("Pinned derived object label differs");
                expectedState_ =
                    patched(expectedState_, {QStringLiteral("objects"), QString::number(i), QStringLiteral("label")}, 0,
                            change.value(QStringLiteral("new")))
                        .toObject();
                ++found;
            }
            if (found != 1) throw std::runtime_error("Missing or ambiguous derived object label");
        }
        QSet<QString> movedObjects;
        for (const auto& value : edit.value(QStringLiteral("object_positions")).toArray()) {
            const QJsonObject move = value.toObject();
            const QString id = move.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || movedObjects.contains(id))
                throw std::runtime_error("Missing or duplicate pinned moved-object ID");
            movedObjects.insert(id);
            const int dx = move.value(QStringLiteral("dx")).toInt(), dy = move.value(QStringLiteral("dy")).toInt();
            if (dx == 0 && dy == 0) throw std::runtime_error("A declared object position must change");
            const QJsonArray objects = before.value(QStringLiteral("objects")).toArray();
            int found = 0;
            for (int i = 0; i < objects.size(); ++i) {
                const auto object = objects.at(i).toObject();
                if (object.value(QStringLiteral("id")).toString() != id) continue;
                for (const auto& delta :
                     QVector<QPair<QString, int>>{{QStringLiteral("x"), dx}, {QStringLiteral("y"), dy}}) {
                    if (delta.second == 0) continue;
                    expectedState_ =
                        patched(expectedState_, {QStringLiteral("objects"), QString::number(i), delta.first}, 0,
                                object.value(delta.first).toInt() + delta.second)
                            .toObject();
                }
                ++found;
            }
            if (found != 1) throw std::runtime_error("Missing or ambiguous moved map object");
        }
        QSet<QString> changedPaths;
        changedPaths.insert(path.join(QLatin1Char('/')));
        for (const auto& value : edit.value(QStringLiteral("state_changes")).toArray()) {
            const QJsonObject change = value.toObject();
            QStringList target;
            for (const auto& part : change.value(QStringLiteral("path")).toArray()) target.append(part.toString());
            const QString address = target.join(QLatin1Char('/'));
            if (target.isEmpty() || changedPaths.contains(address))
                throw std::runtime_error("Missing or duplicate pinned state change address");
            changedPaths.insert(address);
            QJsonValue previous = before;
            for (const auto& part : target)
                previous = previous.isArray() ? previous.toArray().at(part.toInt()) : previous.toObject().value(part);
            if (previous != change.value(QStringLiteral("old")) || previous == change.value(QStringLiteral("new")))
                throw std::runtime_error("Pinned coupled state change precondition differs");
            expectedState_ = patched(expectedState_, target, 0, change.value(QStringLiteral("new"))).toObject();
        }
        results_.insert(QStringLiteral("semantic_expected"), expectedState_);
        screenshot(QStringLiteral("before-edit"));
        editHostMessages_ = window_->hostMessages_.size();
        editSessionMessages_ = window_->session_->diagnostics().size();
        monitorEdit_ = true;
        if (op == QLatin1String("table-cell")) {
            auto* view = qobject_cast<QTableView*>(visibleWidget(QStringLiteral("documentTable")));
            const QModelIndex index = view->model()->index(edit.value(QStringLiteral("row")).toInt(),
                                                           edit.value(QStringLiteral("column")).toInt());
            if (!(index.flags() & Qt::ItemIsEditable)) throw std::runtime_error("Pinned table cell is not editable");
            view->scrollTo(index);
            QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center());
            QTest::keyClick(view, Qt::Key_F2);
            QPointer<QLineEdit> line;
            waitFor(QStringLiteral("table cell editor"), 10, [&] {
                for (auto* candidate : view->findChildren<QLineEdit*>())
                    if (candidate->isVisible()) line = candidate;
                return bool(line);
            });
            QTest::keyClick(line, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(line, desired.toString());
            QTest::keyClick(line, Qt::Key_Return);
        } else if (op == QLatin1String("inspector-field") || op == QLatin1String("composition-inspector"))
            editField(edit.value(QStringLiteral("field")).toString(), desired.toVariant().toString());
        else if (op == QLatin1String("composition-field"))
            editField(QStringLiteral("tile"), edit.value(QStringLiteral("tile_value")).toVariant().toString());
        else if (op == QLatin1String("palette-channel"))
            clickNamed(QStringLiteral("palette.channel.%1.%2")
                           .arg(edit.value(QStringLiteral("channel")).toInt())
                           .arg(desired.toInt()));
        else if (op == QLatin1String("palette-color"))
            clickNamed(QStringLiteral("palette.gamut.%1").arg(edit.value(QStringLiteral("gamut")).toInt()));
        else if (op == QLatin1String("tile-pixel")) {
            auto* pens = qobject_cast<editors::PenRow*>(visibleWidget(QStringLiteral("tilePens")));
            auto* tiles = dynamic_cast<sdk::TileModel*>(window_->model_.get());
            const int pen = desired.toInt();
            if (!pens || !tiles || pen < 0 || pen >= tiles->penCount())
                throw std::runtime_error("Pinned pixel pen is not available");
            QTest::mouseClick(pens, Qt::LeftButton, Qt::NoModifier, pens->penRect(pen).center());
            waitFor(QStringLiteral("pinned pixel pen"), 10, [tiles, pen] { return tiles->currentPen() == pen; });
            auto* canvas = visibleWidget(QStringLiteral("pixelCanvas"));
            const QPoint at(8 + edit.value(QStringLiteral("x")).toInt() * 41 + 20,
                            8 + edit.value(QStringLiteral("y")).toInt() * 41 + 20);
            revealPoint(canvas, at);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at);
        } else if (op == QLatin1String("map-toolbar-cell")) {
            auto* map = mapModel();
            clickSegment(QStringLiteral("mapTools"), QStringLiteral("paint"));
            clickSegment(QStringLiteral("mapBrushes"), QString::number(desired.toInt()));
            waitFor(QStringLiteral("toolbar paint brush"), 10,
                    [map, desired] { return map->currentBrush() == desired.toInt(); });
            auto* canvas = qobject_cast<editors::MapCanvas*>(visibleWidget(QStringLiteral("mapCanvas")));
            const QPoint at = canvasCellCenter(
                canvas, QPoint(edit.value(QStringLiteral("x")).toInt(), edit.value(QStringLiteral("y")).toInt()));
            revealPoint(canvas, at);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at);
        } else if (op == QLatin1String("map-brush-cell")) {
            auto* map = mapModel();
            sdk::Library library = map->sheet();
            QString source = QStringLiteral("sheet");
            if (library.items.isEmpty()) {
                library = map->inspect().library;
                source = QStringLiteral("inspector");
            }
            const QString item =
                edit.value(QStringLiteral("library_item")).toString(QStringLiteral("tile:%1").arg(desired.toInt()));
            int index = -1;
            for (int i = 0; i < library.items.size(); ++i)
                if (library.items.at(i).id == item) index = i;
            if (index < 0) throw std::runtime_error("Pinned pattern missing from the GUI sheet");
            editors::LibrarySheet* sheet = nullptr;
            for (auto* widget : window_->findChildren<QWidget*>(QStringLiteral("librarySheet"))) {
                auto* candidate = dynamic_cast<editors::LibrarySheet*>(widget);
                if (candidate && candidate->isVisibleTo(window_) &&
                    candidate->property("librarySource").toString() == source)
                    sheet = candidate;
            }
            if (!sheet || sheet->itemId(index) != item)
                throw std::runtime_error("Pinned library grid is missing or stale");
            const QPoint pick = sheet->itemRect(index).center();
            revealPoint(sheet, pick);
            QTest::mouseClick(sheet, Qt::LeftButton, Qt::NoModifier, pick);
            waitFor(QStringLiteral("paint brush"), 10, [map, item] { return map->currentLibraryItem() == item; });
            auto* canvas = qobject_cast<editors::MapCanvas*>(visibleWidget(QStringLiteral("mapCanvas")));
            const QPoint at = canvasCellCenter(
                canvas, QPoint(edit.value(QStringLiteral("x")).toInt(), edit.value(QStringLiteral("y")).toInt()));
            revealPoint(canvas, at);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, at);
        } else if (op == QLatin1String("sequence-note")) {
            auto* roll = visibleWidget(QStringLiteral("pianoRoll"));
            const int delta = desired.toInt() - oldValue.toInt();
            for (int i = 0; i < std::abs(delta); ++i) QTest::keyClick(roll, delta > 0 ? Qt::Key_Up : Qt::Key_Down);
        }
        waitFor(QStringLiteral("the exact edited semantic state"), 15,
                [this] { return semanticState() == expectedState_; });
        monitorEdit_ = false;
        results_.insert(QStringLiteral("semantic_after"), semanticState());
        results_.insert(QStringLiteral("semantic_before_sha256"), stateHash(before));
        results_.insert(QStringLiteral("semantic_after_sha256"), stateHash(expectedState_));
        waitFor(QStringLiteral("Undo becomes enabled"), 10, [this] { return window_->undoButton_->isEnabled(); });
        clickButton(window_->undoButton_, QStringLiteral("Undo"));
        waitFor(QStringLiteral("undo restores the exact original state"), 10,
                [this, before] { return semanticState() == before; });
        waitFor(QStringLiteral("Redo becomes enabled"), 10, [this] { return window_->redoButton_->isEnabled(); });
        clickButton(window_->redoButton_, QStringLiteral("Redo"));
        waitFor(QStringLiteral("redo restores the exact edit"), 10,
                [this] { return semanticState() == expectedState_; });
        results_.insert(QStringLiteral("undo_redo_verified"), true);
        verifyPresentation();
    }

    void verifyPresentation() {
        waitForSelectionPresentation();
        const auto edit = scenario_.value(QStringLiteral("edit")).toObject();
        if (operation() == QLatin1String("inspector-field") || operation() == QLatin1String("composition-field") ||
            operation() == QLatin1String("composition-inspector")) {
            const QString key = operation() == QLatin1String("composition-field")
                                    ? QStringLiteral("tile")
                                    : edit.value(QStringLiteral("field")).toString();
            const QString value = operation() == QLatin1String("composition-field")
                                      ? edit.value(QStringLiteral("tile_value")).toVariant().toString()
                                      : edit.value(QStringLiteral("new")).toVariant().toString();
            waitFor(QStringLiteral("the visible inspector displays the edited value"), 10, [this, key, value] {
                for (auto* widget : window_->findChildren<QWidget*>(QStringLiteral("inspector.field.%1").arg(key))) {
                    if (!widget->isVisibleTo(window_)) continue;
                    if (auto* combo = qobject_cast<QComboBox*>(widget)) return combo->currentData().toString() == value;
                    if (auto* line = qobject_cast<QLineEdit*>(widget)) return line->text() == value;
                }
                return false;
            });
        } else if (operation() == QLatin1String("table-cell")) {
            waitFor(QStringLiteral("the table displays the edited value"), 10, [this, edit] {
                auto* view = window_->findChild<QTableView*>(QStringLiteral("documentTable"));
                return view && view->model()
                                       ->data(view->model()->index(edit.value(QStringLiteral("row")).toInt(),
                                                                   edit.value(QStringLiteral("column")).toInt()))
                                       .toString() == edit.value(QStringLiteral("new")).toString();
            });
        }
    }

    void verifyTabEdit() {
        prepareEdit();
        if (semanticState() != expectedState_)
            throw std::runtime_error("The exact tab edit did not survive navigation or save/reopen");
        verifyPresentation();
        results_.insert(QStringLiteral("semantic_reopened_sha256"), stateHash(semanticState()));
    }

    void switchDocuments() {
        const QJsonObject project = scenario_.value(QStringLiteral("project")).toObject();
        const QString kind = project.value(QStringLiteral("kind")).toString();
        const QJsonArray mirrors = scenario_.value(QStringLiteral("mirrored_documents")).toArray();
        QJsonArray verified;
        for (const auto& document : scenario_.value(QStringLiteral("switch_documents")).toArray()) {
            selectDocument(kind, document.toString());
            if (mirrors.contains(document)) {
                if (semanticState() != expectedState_)
                    throw std::runtime_error("A declared shared document does not display the exact expected edit");
                verified.append(document);
            }
        }
        if (!mirrors.isEmpty()) results_.insert(QStringLiteral("mirrored_documents_verified"), verified);
        selectScenarioDocument();
        note(QStringLiteral("navigation"),
             QStringLiteral("Returned to the pinned document after explicit document switches"));
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
        waitFor(QStringLiteral("Undo becomes enabled"), 10, [this] { return window_->undoButton_->isEnabled(); });
        clickButton(window_->undoButton_, QStringLiteral("Undo"));
        waitFor(QStringLiteral("undo restores every original map object and its budget"), 10,
                [this, original] { return objectSnapshot() == original && budgetSnapshot() == originalBudget_; });
        waitFor(QStringLiteral("Redo becomes enabled"), 10, [this] { return window_->redoButton_->isEnabled(); });
        clickButton(window_->redoButton_, QStringLiteral("Redo"));
        waitFor(QStringLiteral("redo restores the exact map edit and budget"), 10,
                [this] { return objectSnapshot() == expectedObjects_ && budgetSnapshot() == originalBudget_; });
        results_.insert(QStringLiteral("undo_redo_verified"), true);
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
        finished_ = true;
        exitCode_ = exitCode;
        dialogTimer_->stop();
        if (status == QLatin1String("FAIL")) {
            try {
                if (window_->model_ && operation() != QLatin1String("move-object-vertical"))
                    results_.insert(QStringLiteral("semantic_at_failure"), semanticState());
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

            begin(QStringLiteral("Wait for the prepared game project"));
            waitFor(QStringLiteral("the prepared project"), timeout("open_s", 900),
                    [this] { return window_->session_ && !window_->isBusy() && !QApplication::activeModalWidget(); });
            if (!window_->session_) throw std::runtime_error("No project session was opened");
            if (!consentHandled_) {
                note(QStringLiteral("dialog"),
                     QStringLiteral("No repository-script consent was requested while opening"));
            }
            const sdk::Profile profile = window_->session_->profile();
            const QJsonObject expectedProject = scenario_.value(QStringLiteral("project")).toObject();
            results_.insert(QStringLiteral("actual_profile"),
                            QJsonObject{{QStringLiteral("name"), profile.name},
                                        {QStringLiteral("revision"), profile.revision},
                                        {QStringLiteral("platform"), profile.platform},
                                        {QStringLiteral("codec"), profile.codec}});
            if (operation() != QLatin1String("inspect") &&
                (profile.name != expectedProject.value(QStringLiteral("name")).toString() ||
                 profile.revision != expectedProject.value(QStringLiteral("revision")).toString() ||
                 profile.platform != expectedProject.value(QStringLiteral("platform")).toString() ||
                 profile.codec != expectedProject.value(QStringLiteral("codec")).toString())) {
                throw std::runtime_error(QStringLiteral("Wrong game project: %1 %2 %3 %4")
                                             .arg(profile.name, profile.revision, profile.platform, profile.codec)
                                             .toStdString());
            }
            QJsonArray availableKinds;
            QStringList actualKinds, expectedKinds;
            for (const auto& kind : window_->session_->kinds()) {
                actualKinds.append(kind.id);
                availableKinds.append(kind.id);
            }
            for (const auto& kind : scenario_.value(QStringLiteral("expected_kinds")).toArray())
                expectedKinds.append(kind.toString());
            results_.insert(QStringLiteral("available_kinds"), availableKinds);
            actualKinds.sort();
            expectedKinds.sort();
            if (!expectedKinds.isEmpty() && actualKinds != expectedKinds)
                throw std::runtime_error("The available tab set changed; coverage needs review");
            projectPath_ = window_->projectPath_;
            results_.insert(QStringLiteral("project_path"), projectPath_);
            screenshot(QStringLiteral("project-ready"));

            if (operation() == QLatin1String("inspect")) {
                inspectDocuments();
                return finish(
                    QStringLiteral("INSPECTED"),
                    QStringLiteral(
                        "Read-only GUI inventory captured. No edit, ROM build or regression PASS is claimed."),
                    4);
            }

            begin(QStringLiteral("Open the pinned tab and document through GUI"));
            selectScenarioDocument();

            begin(QStringLiteral("Apply the pinned %1 edit through widgets").arg(operation()));
            if (operation() == QLatin1String("move-object-vertical")) moveBlock();
            else applyTabEdit();

            begin(QStringLiteral("Verify the edit and unsaved-change indicator"));
            waitFor(QStringLiteral("the GUI unsaved-change indicators"), 10, [this] {
                return window_->session_->isModified() && window_->dirtyIndicator_->isVisible() &&
                       window_->saveButton_->isEnabled();
            });
            screenshot(QStringLiteral("after-edit"));
            begin(QStringLiteral("Switch documents and return to the edited document"));
            switchDocuments();
            if (operation() == QLatin1String("move-object-vertical")) verifyMovedBlock();
            else verifyTabEdit();
            screenshot(QStringLiteral("after-navigation"));

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
            selectScenarioDocument();
            if (operation() == QLatin1String("move-object-vertical")) {
                verifyMovedBlock();
                results_.insert(QStringLiteral("objects_reopened"), objectSnapshot());
                results_.insert(QStringLiteral("budget_reopened"), budgetSnapshot());
            } else verifyTabEdit();
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
            const qint64 expectedSize = qint64(scenario_.value(QStringLiteral("artifact_size")).toDouble(0));
            if (expectedSize > 0 && built.size() != expectedSize)
                throw std::runtime_error(QStringLiteral("Built ROM container has %1 bytes; expected %2")
                                             .arg(built.size())
                                             .arg(expectedSize)
                                             .toStdString());
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
    QJsonObject expectedState_;
    QJsonArray expectedObjects_;
    QJsonObject originalBudget_;
    bool monitorEdit_ = false;
    int editHostMessages_ = 0;
    int editSessionMessages_ = 0;
    int shot_ = 0;
    bool finished_ = false;
    int exitCode_ = 1;
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
    app.exec();
    return test.exitCode();
}
