#include "shell/autotest.hpp"

#include "runtime/emulator_runner.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/kit_session.hpp"
#include "sdk/session.hpp"
#include "shell/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>

namespace rl::shell {

namespace {

constexpr int kPollMs = 200;
constexpr int kSettleMs = 500;
// Edits made in one document, whatever its editor offers.
constexpr int kEditsPerDocument = 5;

QString fileName(QString text) {
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return text.left(80);
}

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

// Up to `count` of `size` indices, spread over the whole range.
QVector<int> spread(int size, int count) {
    QVector<int> picked;
    if (size <= count) {
        for (int i = 0; i < size; ++i) picked << i;
        return picked;
    }
    for (int i = 0; i < count; ++i) {
        const int index = count == 1 ? 0 : i * (size - 1) / (count - 1);
        if (!picked.contains(index)) picked << index;
    }
    return picked;
}

bool hasTool(const QVector<sdk::Tool>& tools, const QString& id) {
    return std::any_of(tools.begin(), tools.end(), [&id](const sdk::Tool& tool) { return tool.id == id; });
}

QString cellText(QPoint cell) { return QStringLiteral("%1,%2").arg(cell.x()).arg(cell.y()); }

// Cells of a map from its middle outwards, so edits land where the content is.
QVector<QPoint> cellsFromMiddle(QSize grid) {
    QVector<QPoint> cells;
    for (int y = 0; y < grid.height(); ++y) {
        for (int x = 0; x < grid.width(); ++x) cells << QPoint(x, y);
    }
    const QPoint middle(grid.width() / 2, grid.height() / 2);
    std::stable_sort(cells.begin(), cells.end(), [middle](QPoint a, QPoint b) {
        return (a - middle).manhattanLength() < (b - middle).manhattanLength();
    });
    return cells;
}

}  // namespace

Autotest::Autotest(MainWindow* window, const QString& scenarioPath) : QObject(window), window_(window) {
    QFile file(scenarioPath);
    if (file.open(QIODevice::ReadOnly)) {
        scenario_ = QJsonDocument::fromJson(file.readAll()).object();
    }
    output_ = QDir(scenario_.value(QStringLiteral("output")).toString());
    output_.mkpath(QStringLiteral("."));
    results_.insert(QStringLiteral("scenario"), scenario_);
}

void Autotest::start() {
    clock_.start();
    if (scenario_.isEmpty()) {
        note(QStringLiteral("error"), QStringLiteral("the scenario could not be read"));
        failed_ = true;
        finish();
        return;
    }
    const QJsonObject settings = scenario_.value(QStringLiteral("settings")).toObject();
    QSettings stored;
    for (auto it = settings.begin(); it != settings.end(); ++it) stored.setValue(it.key(), it.value().toVariant());
    auto* dialogs = new QTimer(this);
    dialogs->setInterval(kSettleMs);
    connect(dialogs, &QTimer::timeout, this, &Autotest::watchDialogs);
    dialogs->start();
    after(kSettleMs, [this] { openRom(); });
}

void Autotest::after(int ms, const Step& then) {
    QTimer::singleShot(ms, this, [this, then] {
        try {
            then();
        } catch (const std::exception& e) {
            note(QStringLiteral("error"),
                 QStringLiteral("%1 failed: %2")
                     .arg(touring_ ? current_ : QStringLiteral("step"), QString::fromUtf8(e.what())));
            failed_ = true;
            if (touring_) {
                after(kSettleMs, [this] { visitNext(); });
            } else {
                after(kSettleMs, [this] { finish(); });
            }
        }
    });
}

void Autotest::waitFor(const std::function<bool()>& ready, int timeoutMs, const QString& what, const Step& then) {
    auto waited = std::make_shared<QElapsedTimer>();
    waited->start();
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [this, ready, timeoutMs, what, then, waited, poll] {
        if (ready()) {
            then();
            return;
        }
        if (waited->elapsed() > timeoutMs) {
            note(QStringLiteral("error"), QStringLiteral("timed out after %1 s: %2").arg(timeoutMs / 1000).arg(what));
            failed_ = true;
            then();
            return;
        }
        after(kPollMs, *poll);
    };
    after(kPollMs, *poll);
}

void Autotest::note(const QString& kind, const QString& text) {
    events_.append(QJsonObject{{QStringLiteral("at_ms"), double(clock_.elapsed())},
                               {QStringLiteral("kind"), kind},
                               {QStringLiteral("text"), text}});
}

QString Autotest::screenshot(const QString& name) {
    const QString file = QStringLiteral("%1-%2.png").arg(shot_++, 3, 10, QLatin1Char('0')).arg(fileName(name));
    window_->grab().save(output_.filePath(file), "PNG");
    return file;
}

QJsonArray Autotest::takeMessages() {
    QJsonArray messages;
    for (; seenMessages_ < window_->hostMessages_.size(); ++seenMessages_) {
        const sdk::Diagnostic& d = window_->hostMessages_.at(seenMessages_);
        messages.append(QJsonObject{{QStringLiteral("severity"), severityName(d.severity)},
                                    {QStringLiteral("source"), d.source},
                                    {QStringLiteral("message"), d.message}});
    }
    return messages;
}

void Autotest::watchDialogs() {
    QWidget* modal = QApplication::activeModalWidget();
    if (!modal || modal == window_) return;
    // A dialog no step expects: what it says, then it goes.
    QStringList texts;
    for (const QLabel* label : modal->findChildren<QLabel*>()) {
        if (!label->text().trimmed().isEmpty()) texts << label->text().trimmed();
    }
    const QString file = QStringLiteral("%1-dialog.png").arg(shot_++, 3, 10, QLatin1Char('0'));
    modal->grab().save(output_.filePath(file), "PNG");
    note(QStringLiteral("dialog"), texts.join(QStringLiteral(" | ")) + QStringLiteral(" [") + file + QLatin1Char(']'));
    ++dialogs_;
    if (auto* dialog = qobject_cast<QDialog*>(modal)) {
        dialog->reject();
    } else {
        modal->close();
    }
}

// ---------------------------------------------------------------- steps

void Autotest::openRom() {
    const QString rom = scenario_.value(QStringLiteral("rom")).toString();
    note(QStringLiteral("step"), QStringLiteral("open ROM %1").arg(rom));
    window_->openRomFile(rom);
    const int timeout = scenario_.value(QStringLiteral("open_timeout_s")).toInt(900) * 1000;
    // Open, or given up on: a dialog says why (a download that failed).
    const int dialogs = dialogs_;
    waitFor([this, dialogs] { return (window_->session_ || dialogs_ > dialogs) && !window_->isBusy(); }, timeout,
            QStringLiteral("the ROM's project to open"),
            [this] {
                results_.insert(QStringLiteral("project"), window_->projectPath_);
                results_.insert(QStringLiteral("opened"), screenshot(QStringLiteral("opened")));
                results_.insert(QStringLiteral("open_messages"), takeMessages());
                if (!window_->session_) {
                    failed_ = true;
                    finish();
                    return;
                }
                after(kSettleMs, [this] { tourDocuments(); });
            });
}

void Autotest::tourDocuments() {
    const int perKind = scenario_.value(QStringLiteral("documents_per_kind")).toInt(4);
    for (const sdk::Kind& kind : window_->session_->kinds()) {
        QVector<QString> ids;
        for (const sdk::DocumentGroup& group : window_->session_->documents(kind.id)) {
            for (const sdk::DocumentEntry& entry : group.documents) ids << entry.id;
        }
        for (int index : spread(ids.size(), perKind)) queue_ << qMakePair(kind.id, ids.at(index));
    }
    note(QStringLiteral("step"), QStringLiteral("visit %1 documents").arg(queue_.size()));
    touring_ = true;
    visitNext();
}

void Autotest::visitNext() {
    if (queue_.isEmpty()) {
        touring_ = false;
        saveBuildAndPlay();
        return;
    }
    const auto [kind, document] = queue_.takeFirst();
    current_ = kind + QLatin1Char('/') + document;
    window_->showDocument(kind, document);
    // Opening some documents runs the kit (an upgrade); the editor follows.
    waitFor([this] { return !window_->isBusy(); }, 300000, QStringLiteral("%1/%2 to open").arg(kind, document),
            [this, kind = kind, document = document] {
                after(kSettleMs, [this, kind, document] {
                    auto entry = std::make_shared<QJsonObject>();
                    entry->insert(QStringLiteral("kind"), kind);
                    entry->insert(QStringLiteral("document"), document);
                    sdk::EditorModel* model = window_->model_.get();
                    entry->insert(QStringLiteral("title"), model ? model->title() : QString());
                    entry->insert(QStringLiteral("editor"), model ? int(model->type()) : -1);
                    entry->insert(QStringLiteral("view"), screenshot(kind + QLatin1Char('-') + document));
                    const bool edit = scenario_.value(QStringLiteral("edits")).toBool(true) && model &&
                                      !model->isReadOnly() && model->type() != sdk::EditorType::Emulator;
                    if (!edit) {
                        entry->insert(QStringLiteral("messages"), takeMessages());
                        documents_.append(*entry);
                        visitNext();
                        return;
                    }
                    QJsonArray edits;
                    editDocument(edits);
                    entry->insert(QStringLiteral("edits"), edits);
                    after(kSettleMs, [this, entry, kind, document] {
                        entry->insert(QStringLiteral("edited"),
                                      screenshot(kind + QLatin1Char('-') + document + QStringLiteral("-edited")));
                        entry->insert(QStringLiteral("messages"), takeMessages());
                        documents_.append(*entry);
                        visitNext();
                    });
                });
            });
}

void Autotest::record(QJsonArray& edits, const QString& what, bool ok, const QString& error) {
    QJsonObject edit{{QStringLiteral("what"), what}, {QStringLiteral("ok"), ok}};
    if (!error.isEmpty()) edit.insert(QStringLiteral("error"), error);
    edits.append(edit);
}

void Autotest::editDocument(QJsonArray& edits) {
    sdk::EditorModel* model = window_->model_.get();
    QUndoStack* undo = window_->session_ ? window_->session_->undoStack() : nullptr;
    const int undoBefore = undo ? undo->index() : -1;
    QString error;

    if (auto* map = dynamic_cast<sdk::TileMapModel*>(model)) {
        QVector<sdk::MapLayer> layers = map->layers();
        if (layers.isEmpty()) layers << sdk::MapLayer{};
        for (const sdk::MapLayer& layer : layers) {
            if (edits.size() >= kEditsPerDocument) break;
            if (!layer.id.isEmpty()) map->setCurrentLayer(layer.id);
            const QString on = layer.id.isEmpty() ? QString() : QStringLiteral(" on ") + layer.id;
            const QVector<sdk::Tool> tools = map->tools();
            const QVector<QPoint> cells = cellsFromMiddle(map->gridSize());
            if (hasTool(tools, QStringLiteral("paint"))) {
                const auto cell =
                    std::find_if(cells.begin(), cells.end(), [map](QPoint c) { return map->isCellEditable(c); });
                if (cell != cells.end()) {
                    const int was = map->cellValue(*cell);
                    int value = map->currentBrush();
                    for (const sdk::CellBrush& brush : map->brushes()) {
                        if (brush.value != was) {
                            value = brush.value;
                            break;
                        }
                    }
                    if (value == was) {
                        // A pattern sheet chooses the brush.
                        for (const sdk::LibraryItem& item : map->sheet().items) {
                            map->setCurrentLibraryItem(item.id);
                            if (map->currentBrush() != was) break;
                        }
                        value = map->currentBrush();
                    }
                    map->beginStroke();
                    const bool painted = map->paintCell(*cell, value);
                    map->endStroke();
                    record(edits, QStringLiteral("paint %1 = %2%3").arg(cellText(*cell)).arg(value).arg(on),
                           painted && map->cellValue(*cell) == value);
                }
            }
            if (hasTool(tools, QStringLiteral("place"))) {
                QVector<sdk::LibraryItem> items = map->inspect().library.items;
                if (items.isEmpty()) items = map->sheet().items;
                const QString start = map->startLibraryItem();
                const auto item = std::find_if(items.begin(), items.end(), [&start](const sdk::LibraryItem& candidate) {
                    return candidate.id != start;
                });
                if (item != items.end()) {
                    const QPoint cell = cells.value(cells.size() / 3, QPoint(1, 1));
                    error.clear();
                    const bool placed = map->placeObject(item->id, cell, &error);
                    record(edits, QStringLiteral("place %1 at %2%3").arg(item->id, cellText(cell), on), placed, error);
                }
            }
            const QVector<sdk::MapObject> objects = map->objects();
            if (!objects.isEmpty() && hasTool(tools, QStringLiteral("select"))) {
                const sdk::MapObject& object = objects.first();
                map->beginDrag(object.id);
                const bool moved = map->dragObject(object.id, object.cells.topLeft() + QPoint(1, 0));
                map->endDrag();
                record(edits, QStringLiteral("drag %1 one cell right%2").arg(object.id, on), moved);
            }
        }
    } else if (auto* tile = dynamic_cast<sdk::TileModel*>(model)) {
        const QVector<quint8> pixels = tile->tilePixels(tile->currentTile());
        const QPoint pixel(1, 1);
        const int index = pixel.y() * tile->tileSize().width() + pixel.x();
        const int pen = (pixels.value(index) + 1) % std::max(2, tile->penCount());
        tile->beginStroke();
        const bool drawn = tile->setPixel(pixel, pen);
        tile->endStroke();
        record(edits, QStringLiteral("tile %1 pixel 1,1 = pen %2").arg(tile->currentTile()).arg(pen),
               drawn && tile->tilePixels(tile->currentTile()).value(index) == pen);
    } else if (auto* palette = dynamic_cast<sdk::PaletteModel*>(model)) {
        error.clear();
        if (!palette->gamut().isEmpty()) {
            const int next = (palette->currentGamutIndex() + 1) % palette->gamut().size();
            record(edits, QStringLiteral("palette entry = gamut %1").arg(next), palette->assignGamut(next, &error),
                   error);
        } else if (!palette->currentChannels().isEmpty()) {
            const int next = (palette->currentChannels().first() + 1) % std::max(2, palette->channelLevels());
            record(edits, QStringLiteral("palette channel 0 = %1").arg(next), palette->setChannel(0, next, &error),
                   error);
        }
    } else if (auto* table = dynamic_cast<sdk::TableModel*>(model)) {
        const int row = std::min(1, table->rowCount() - 1);
        for (int column = 0; row >= 0 && column < table->columns().size(); ++column) {
            const QString was = table->cell(row, column);
            const QStringList choices = table->cellChoices(row, column);
            QString value;
            if (choices.size() > 1) {
                value = choices.at((std::max(0, int(choices.indexOf(was))) + 1) % choices.size());
            } else {
                bool number = false;
                const int n = was.toInt(&number);
                if (!number) continue;
                value = QString::number(n + 1);
            }
            error.clear();
            record(edits, QStringLiteral("row %1 column %2 = %3").arg(row).arg(column).arg(value),
                   table->setCell(row, column, value, &error), error);
            break;
        }
    } else if (auto* hex = dynamic_cast<sdk::HexModel*>(model)) {
        const int offset = hex->size() / 2;
        const QByteArray was = hex->bytes(offset, 1);
        error.clear();
        const bool written =
            hex->writeBytes(offset, QByteArray(1, char(~(was.isEmpty() ? 0 : was.at(0)))), QString(), &error);
        record(edits, QStringLiteral("byte %1 inverted").arg(offset, 0, 16), written, error);
    } else if (auto* composition = dynamic_cast<sdk::CompositionModel*>(model)) {
        const QVector<sdk::CompositionPiece> pieces = composition->pieces();
        const auto piece =
            std::find_if(pieces.begin(), pieces.end(), [](const sdk::CompositionPiece& p) { return p.movable; });
        if (piece != pieces.end()) {
            composition->beginDrag(piece->id);
            const bool moved = composition->dragPiece(piece->id, QPoint(1, 0));
            composition->endDrag();
            record(edits, QStringLiteral("piece %1 one pixel right").arg(piece->id), moved);
        }
        if (composition->frames().size() > 1) {
            composition->setCurrentFrame(1);
            record(edits, QStringLiteral("frame 2 shown"), composition->currentFrame() == 1);
        }
    } else if (auto* sequencer = dynamic_cast<sdk::SequencerModel*>(model)) {
        const QVector<sdk::SequencerNote> notes = sequencer->notes(sequencer->currentChannel());
        const auto note = std::find_if(notes.begin(), notes.end(),
                                       [](const sdk::SequencerNote& n) { return n.pitch >= 0 && n.pitchEditable; });
        if (note != notes.end()) {
            sequencer->beginEdit(note->id);
            const bool edited = sequencer->editNote(note->id, note->pitch + 1, note->length);
            sequencer->endEdit();
            record(edits, QStringLiteral("note %1 a semitone up").arg(note->id), edited);
        }
    }

    // One inspector field, as the user would change it.
    if (model && edits.size() < kEditsPerDocument) {
        for (const sdk::Field& field : model->inspect().fields) {
            if (field.type != sdk::Field::Type::Choice || field.choices.size() < 2) continue;
            const QString was = field.value.toString();
            const QString value =
                field.choices.at((std::max(0, int(field.choices.indexOf(was))) + 1) % field.choices.size());
            error.clear();
            record(edits, QStringLiteral("field %1 = %2").arg(field.key, value),
                   model->setField(field.key, value, &error), error);
            break;
        }
    }

    // Undo takes the last edit back and redo puts it again.
    if (undo && undo->index() > undoBefore) {
        const int index = undo->index();
        undo->undo();
        const bool undone = undo->index() == index - 1;
        undo->redo();
        record(edits, QStringLiteral("undo and redo"), undone && undo->index() == index);
    }
}

void Autotest::saveBuildAndPlay() {
    results_.insert(QStringLiteral("documents"), documents_);
    note(QStringLiteral("step"), QStringLiteral("save"));
    window_->saveAction_->trigger();
    waitFor(
        [this] { return !window_->isBusy(); }, 120000, QStringLiteral("saving"),
        [this] {
            results_.insert(QStringLiteral("saved"), screenshot(QStringLiteral("saved")));
            results_.insert(QStringLiteral("save_messages"), takeMessages());
            if (!scenario_.value(QStringLiteral("build")).toBool(true) || !window_->buildAction_->isEnabled()) {
                finish();
                return;
            }
            note(QStringLiteral("step"), QStringLiteral("build"));
            window_->buildAction_->trigger();
            waitFor(
                [this] { return !window_->isBusy(); }, 900000, QStringLiteral("the build"),
                [this] {
                    const QJsonArray messages = takeMessages();
                    bool built = true;
                    for (const QJsonValue& message : messages) {
                        if (message.toObject().value(QStringLiteral("severity")).toString() == QLatin1String("error"))
                            built = false;
                    }
                    if (auto* kit = dynamic_cast<sdk::KitSession*>(window_->session_.get())) {
                        built = built && !kit->lastBuild().isEmpty();
                    }
                    failed_ = failed_ || !built;
                    results_.insert(QStringLiteral("build"),
                                    QJsonObject{{QStringLiteral("ok"), built},
                                                {QStringLiteral("messages"), messages},
                                                {QStringLiteral("screenshot"), screenshot(QStringLiteral("built"))}});
                    // From the selected start when the game has one, else from
                    // power-on.
                    QAction* play =
                        window_->playSelectedAction_->isEnabled() ? window_->playSelectedAction_ : window_->playAction_;
                    if (!built || !scenario_.value(QStringLiteral("play")).toBool(true) || !play->isEnabled()) {
                        finish();
                        return;
                    }
                    note(QStringLiteral("step"), play == window_->playAction_ ? QStringLiteral("play from power-on")
                                                                              : QStringLiteral("play from selected"));
                    play->trigger();
                    const int playTimeout = scenario_.value(QStringLiteral("play_timeout_s")).toInt(300) * 1000;
                    waitFor([this] { return window_->emulator_->phase() == runtime::EmulatorRunner::Phase::Running; },
                            playTimeout, QStringLiteral("the game to run"),
                            [this] {
                                after(scenario_.value(QStringLiteral("play_seconds")).toInt(5) * 1000, [this] {
                                    const bool running =
                                        window_->emulator_->phase() == runtime::EmulatorRunner::Phase::Running;
                                    const QString picture =
                                        QStringLiteral("%1-game.png").arg(shot_++, 3, 10, QLatin1Char('0'));
                                    window_->emulator_->picture().save(output_.filePath(picture), "PNG");
                                    failed_ = failed_ || !running;
                                    results_.insert(QStringLiteral("play"),
                                                    QJsonObject{{QStringLiteral("ok"), running},
                                                                {QStringLiteral("picture"), picture},
                                                                {QStringLiteral("screenshot"),
                                                                 screenshot(QStringLiteral("playing"))},
                                                                {QStringLiteral("messages"), takeMessages()}});
                                    window_->stopEmulator();
                                    waitFor(
                                        [this] {
                                            return window_->emulator_->phase() == runtime::EmulatorRunner::Phase::Idle;
                                        },
                                        20000, QStringLiteral("the game to stop"), [this] { finish(); });
                                });
                            });
                });
        });
}

void Autotest::finish() {
    if (!results_.contains(QStringLiteral("documents"))) results_.insert(QStringLiteral("documents"), documents_);
    results_.insert(QStringLiteral("events"), events_);
    results_.insert(QStringLiteral("messages"), takeMessages());
    results_.insert(QStringLiteral("ok"), !failed_);
    results_.insert(QStringLiteral("seconds"), double(clock_.elapsed()) / 1000.0);
    QFile file(output_.filePath(QStringLiteral("results.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(results_).toJson(QJsonDocument::Indented));
    }
    // The open project's edits stay in the run's own home; nothing asks.
    QApplication::exit(failed_ ? 1 : 0);
}

}  // namespace rl::shell
