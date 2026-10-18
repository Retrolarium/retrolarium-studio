#include "shell/tas_editor.hpp"

#include "runtime/emulator_runner.hpp"
#include "sdk/i18n.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPushButton>
#include <QSaveFile>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace rl::shell {

// ---------------------------------------------------------------- the file

QString EditedMovie::platform() const {
    for (const QString& line : header) {
        if (line.startsWith(QLatin1String("platform "))) return line.mid(9).trimmed();
    }
    return QString();
}

bool readMovie(const QString& path, EditedMovie* movie, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = file.errorString();
        return false;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    if (lines.value(0).trimmed() != QLatin1String("# retrolarium movie 1")) {
        *error = QObject::tr("not a Retrolarium movie");
        return false;
    }
    EditedMovie read;
    int i = 1;
    for (; i < lines.size() && lines.at(i).trimmed() != QLatin1String("---"); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1String("frames "))) continue;
        if (line.startsWith(QLatin1String("offset "))) {
            read.offset = line.mid(7).toInt();
            continue;
        }
        if (!line.isEmpty()) read.header << line;
    }
    for (++i; i < lines.size(); ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        bool ok = false;
        const int count = parts.value(0).toInt(&ok);
        if (!ok || parts.size() < 3 || count < 1) {
            *error = QObject::tr("line %1 is not a run of frames").arg(i + 1);
            return false;
        }
        MovieFrame frame;
        frame.pad1 = quint8(parts.at(1).toUInt(nullptr, 16));
        frame.pad2 = quint8(parts.at(2).toUInt(nullptr, 16));
        read.frames.append(MovieFrame{frame.pad1, frame.pad2, parts.value(3)});
        for (int k = 1; k < count; ++k) read.frames.append(frame);
    }
    *movie = read;
    return true;
}

bool writeMovie(const QString& path, const EditedMovie& movie, QString* error) {
    QString text = QStringLiteral("# retrolarium movie 1\n");
    for (const QString& line : movie.header) text += line + QLatin1Char('\n');
    text += QStringLiteral("frames %1\noffset %2\n---\n").arg(movie.frames.size()).arg(movie.offset);
    // Runs of equal frames, as the kit writes them: a frame with a command
    // starts a run of its own.
    int i = 0;
    while (i < movie.frames.size()) {
        const MovieFrame& frame = movie.frames.at(i);
        int j = i + 1;
        if (frame.command.isEmpty()) {
            while (j < movie.frames.size() && movie.frames.at(j) == frame) ++j;
        }
        text += QStringLiteral("%1 %2 %3")
                    .arg(j - i)
                    .arg(frame.pad1, 2, 16, QLatin1Char('0'))
                    .arg(frame.pad2, 2, 16, QLatin1Char('0'));
        if (!frame.command.isEmpty()) text += QLatin1Char(' ') + frame.command;
        text += QLatin1Char('\n');
        i = j;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- model

MovieTableModel::MovieTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void MovieTableModel::setMovie(const EditedMovie& movie) {
    beginResetModel();
    movie_ = movie;
    // MAME's masks: the NES joypad, the Mega Drive 3-button pad.
    if (movie_.platform() == QLatin1String("megadrive")) {
        buttons_ = {{QStringLiteral("←"), 0x04}, {QStringLiteral("→"), 0x08}, {QStringLiteral("↑"), 0x01},
                    {QStringLiteral("↓"), 0x02}, {QStringLiteral("A"), 0x40},  {QStringLiteral("B"), 0x10},
                    {QStringLiteral("C"), 0x20}, {QStringLiteral("St"), 0x80}};
    } else {
        buttons_ = {{QStringLiteral("←"), 0x40}, {QStringLiteral("→"), 0x80}, {QStringLiteral("↑"), 0x10},
                    {QStringLiteral("↓"), 0x20}, {QStringLiteral("B"), 0x02},  {QStringLiteral("A"), 0x01},
                    {QStringLiteral("Se"), 0x04}, {QStringLiteral("St"), 0x08}};
    }
    endResetModel();
}

void MovieTableModel::splice(int row, int count, const QVector<MovieFrame>& frames) {
    if (count == frames.size()) {
        for (int i = 0; i < count; ++i) movie_.frames[row + i] = frames.at(i);
        if (count > 0) emit dataChanged(index(row, 0), index(row + count - 1, columnCount() - 1));
        return;
    }
    if (count > 0) {
        beginRemoveRows(QModelIndex(), row, row + count - 1);
        movie_.frames.remove(row, count);
        endRemoveRows();
    }
    if (!frames.isEmpty()) {
        beginInsertRows(QModelIndex(), row, row + frames.size() - 1);
        for (int i = 0; i < frames.size(); ++i) movie_.frames.insert(row + i, frames.at(i));
        endInsertRows();
    }
    emit headerDataChanged(Qt::Vertical, row, std::max(row, rowCount() - 1));
}

int MovieTableModel::buttonMask(int column) const {
    return column >= 0 && column < buttons_.size() ? buttons_.at(column).mask : 0;
}

void MovieTableModel::setCurrentFrame(int frame) {
    if (frame == current_) return;
    const int before = current_;
    current_ = frame;
    for (const int row : {before - 1, frame - 1}) {
        if (row >= 0 && row < rowCount()) emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    }
}

int MovieTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : movie_.frames.size();
}

int MovieTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : buttons_.size();
}

QVariant MovieTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const MovieFrame& frame = movie_.frames.at(index.row());
    const bool held = (frame.pad1 & buttons_.at(index.column()).mask) != 0;
    const theme::Tokens& t = theme::current();
    switch (role) {
    case Qt::DisplayRole:
        return held ? buttons_.at(index.column()).label : QString();
    case Qt::TextAlignmentRole:
        return int(Qt::AlignCenter);
    case Qt::BackgroundRole:
        if (index.row() == current_ - 1) return held ? t.accent : t.hover;
        return held ? t.accentSoft : QVariant();
    case Qt::ForegroundRole:
        return held ? (index.row() == current_ - 1 ? t.onAccent : t.accentInk) : t.text;
    default:
        return {};
    }
}

QVariant MovieTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole && section < buttons_.size()) return buttons_.at(section).label;
        return {};
    }
    if (role == Qt::DisplayRole) {
        // Frame n: the picture after n frames, made with the input of frame n.
        const QString command = movie_.frames.value(section).command;
        return command.isEmpty() ? QString::number(section + 1)
                                 : QStringLiteral("%1 %2").arg(section + 1).arg(command);
    }
    if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

// ---------------------------------------------------------------- view

MovieTableView::MovieTableView(QWidget* parent) : QTableView(parent) {}

void MovieTableView::mousePressEvent(QMouseEvent* event) {
    const QModelIndex index = indexAt(event->pos());
    auto* table = qobject_cast<MovieTableModel*>(model());
    if (event->button() != Qt::LeftButton || !index.isValid() || table == nullptr) {
        QTableView::mousePressEvent(event);
        return;
    }
    const int mask = table->buttonMask(index.column());
    painting_ = true;
    column_ = index.column();
    value_ = (table->movie().frames.at(index.row()).pad1 & mask) == 0;
    first_ = last_ = index.row();
    before_ = {table->movie().frames.at(index.row())};
    paintAt(index);
    event->accept();
}

void MovieTableView::mouseMoveEvent(QMouseEvent* event) {
    if (!painting_) {
        QTableView::mouseMoveEvent(event);
        return;
    }
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid()) paintAt(model()->index(index.row(), column_));
    event->accept();
}

void MovieTableView::paintAt(const QModelIndex& index) {
    auto* table = qobject_cast<MovieTableModel*>(model());
    const int row = index.row();
    // The frames between the last one painted and this one take it too,
    // and the frames before the edit are kept for its undo.
    const int from = std::min(row, last_), to = std::max(row, last_);
    if (row < first_) {
        QVector<MovieFrame> more;
        for (int r = row; r < first_; ++r) more.append(table->movie().frames.at(r));
        before_ = more + before_;
        first_ = row;
    }
    while (first_ + before_.size() <= to) before_.append(table->movie().frames.at(first_ + before_.size()));
    const int mask = table->buttonMask(column_);
    for (int r = from; r <= to; ++r) {
        MovieFrame frame = table->movie().frames.at(r);
        const quint8 pad = value_ ? quint8(frame.pad1 | mask) : quint8(frame.pad1 & ~mask);
        if (pad != frame.pad1) {
            frame.pad1 = pad;
            table->splice(r, 1, {frame});
        }
    }
    last_ = row;
}

void MovieTableView::mouseReleaseEvent(QMouseEvent* event) {
    if (!painting_) {
        QTableView::mouseReleaseEvent(event);
        return;
    }
    painting_ = false;
    auto* table = qobject_cast<MovieTableModel*>(model());
    const QVector<MovieFrame> after = table->movie().frames.mid(first_, before_.size());
    if (after != before_) emit painted(first_, before_, after);
    event->accept();
}

// ---------------------------------------------------------------- window

namespace {

// An edit of the movie: frames from a row replaced. The first redo was
// done already (a paint); the others, and the undo, replace again.
class SpliceCommand final : public QUndoCommand {
public:
    SpliceCommand(MovieTableModel* model, int row, QVector<MovieFrame> before, QVector<MovieFrame> after,
                  bool done, std::function<void(int)> applied, const QString& text)
        : QUndoCommand(text), model_(model), row_(row), before_(std::move(before)), after_(std::move(after)),
          done_(done), applied_(std::move(applied)) {}

    void redo() override {
        if (!done_) model_->splice(row_, before_.size(), after_);
        done_ = false;
        applied_(row_);
    }
    void undo() override {
        model_->splice(row_, after_.size(), before_);
        applied_(row_);
    }

private:
    MovieTableModel* model_;
    int row_;
    QVector<MovieFrame> before_;
    QVector<MovieFrame> after_;
    bool done_;
    std::function<void(int)> applied_;
};

QPushButton* toolButton(const QString& text, const QString& tip, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setAutoDefault(false);
    button->setFont(theme::font(theme::TextRole::Body, 13.0));
    button->setToolTip(tip);
    return button;
}

}  // namespace

TasEditorWindow::TasEditorWindow(runtime::EmulatorRunner* emulator, QWidget* parent)
    : StudioDialog(tr("TAS editor"), parent), emulator_(emulator), model_(new MovieTableModel(this)),
      view_(new MovieTableView(this)), undo_(new QUndoStack(this)) {
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(460, 720);
    auto* layout = new QVBoxLayout(body());
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(8);

    auto* row1 = new QHBoxLayout;
    QPushButton* saveButton = toolButton(tr("Save"), tr("Write the movie to its file (Ctrl+S)"), this);
    QPushButton* undoButton = toolButton(tr("Undo"), tr("Undo the last edit (Ctrl+Z)"), this);
    QPushButton* redoButton = toolButton(tr("Redo"), tr("Redo (Ctrl+Y)"), this);
    QPushButton* insertButton =
        toolButton(tr("Insert"), tr("Insert empty frames before the selected ones (Insert)"), this);
    QPushButton* copyButton =
        toolButton(tr("Duplicate"), tr("Insert a copy of the selected frames (Ctrl+Insert)"), this);
    QPushButton* removeButton = toolButton(tr("Delete"), tr("Delete the selected frames (Delete)"), this);
    for (QPushButton* button : {saveButton, undoButton, redoButton, insertButton, copyButton, removeButton}) {
        row1->addWidget(button);
    }
    row1->addStretch(1);
    layout->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    QPushButton* goButton = toolButton(tr("Go to frame"), tr("Show the selected frame in the game (Enter)"), this);
    QPushButton* runButton = toolButton(tr("Run to"), tr("Run the movie on until this happens, or to its end"), this);
    stops_ = new QComboBox(this);
    stops_->setFont(theme::font(theme::TextRole::Body, 13.0));
    QPushButton* recordButton =
        toolButton(tr("Record from here"), tr("Record from the frame shown on, dropping the frames after it"), this);
    follow_ = new QCheckBox(tr("Follow"), this);
    follow_->setFont(theme::font(theme::TextRole::Body, 13.0));
    follow_->setToolTip(tr("Keep the frame the game shows in view"));
    follow_->setChecked(true);
    row2->addWidget(goButton);
    row2->addWidget(runButton);
    row2->addWidget(stops_, 1);
    row2->addWidget(recordButton);
    row2->addWidget(follow_);
    layout->addLayout(row2);

    status_ = new QLabel(this);
    status_->setFont(theme::font(theme::TextRole::Body, 12.0));
    layout->addWidget(status_);

    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setShowGrid(true);
    view_->setWordWrap(false);
    view_->setFont(theme::font(theme::TextRole::Body, 12.0));
    view_->verticalHeader()->setDefaultSectionSize(20);
    view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view_->horizontalHeader()->setHighlightSections(false);
    layout->addWidget(view_, 1);

    connect(view_, &MovieTableView::painted, this,
            [this](int row, const QVector<MovieFrame>& before, const QVector<MovieFrame>& after) {
                undo_->push(new SpliceCommand(model_, row, before, after, true, [this](int r) { applied(r); },
                                              tr("Change buttons")));
            });
    // A frame number clicked shows that frame in the game.
    connect(view_->verticalHeader(), &QHeaderView::sectionClicked, this,
            [this](int row) { emulator_->seek(row + 1); });
    connect(view_, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) { emulator_->seek(index.row() + 1); });
    connect(goButton, &QPushButton::clicked, this, [this] {
        const QVector<int> rows = selectedRows();
        if (!rows.isEmpty()) emulator_->seek(rows.first() + 1);
    });
    connect(runButton, &QPushButton::clicked, this, [this] {
        if (stops_->currentIndex() >= 0) emulator_->runUntil(stops_->currentIndex());
    });
    connect(recordButton, &QPushButton::clicked, this, [this] { emulator_->recordFromHere(); });
    connect(saveButton, &QPushButton::clicked, this, &TasEditorWindow::save);
    connect(undoButton, &QPushButton::clicked, undo_, &QUndoStack::undo);
    connect(redoButton, &QPushButton::clicked, undo_, &QUndoStack::redo);
    connect(insertButton, &QPushButton::clicked, this, [this] { insertFrames(false); });
    connect(copyButton, &QPushButton::clicked, this, [this] { insertFrames(true); });
    connect(removeButton, &QPushButton::clicked, this, &TasEditorWindow::removeFrames);
    connect(undo_, &QUndoStack::canUndoChanged, undoButton, &QPushButton::setEnabled);
    connect(undo_, &QUndoStack::canRedoChanged, redoButton, &QPushButton::setEnabled);
    connect(undo_, &QUndoStack::cleanChanged, this, [this] { updateStatus(); });
    undoButton->setEnabled(false);
    redoButton->setEnabled(false);
    connect(emulator_, &runtime::EmulatorRunner::emulationChanged, this, &TasEditorWindow::followEmulator);
    connect(emulator_, &runtime::EmulatorRunner::emulationEvent, this, &TasEditorWindow::onEmulationEvent);
}

bool TasEditorWindow::open(const QString& working, const QString& origin, QString* error) {
    EditedMovie movie;
    if (!readMovie(working, &movie, error)) return false;
    working_ = working;
    origin_ = origin.isEmpty() ? working : origin;
    undo_->clear();
    model_->setMovie(movie);
    stops_->clear();
    for (const runtime::StopCondition& stop : emulator_->stopConditions()) stops_->addItem(stop.name);
    stops_->setEnabled(stops_->count() > 0);
    setWindowTitle(tr("TAS editor — %1").arg(QFileInfo(origin_).fileName()));
    followEmulator();
    updateStatus();
    return true;
}

void TasEditorWindow::edit(int row, int count, const QVector<MovieFrame>& frames, const QString& text) {
    undo_->push(new SpliceCommand(model_, row, model_->movie().frames.mid(row, count), frames, false,
                                  [this](int r) { applied(r); }, text));
}

void TasEditorWindow::applied(int row) {
    // The run plays the working file: write it, let the run take it in, and
    // go back to the last frame the edit leaves as it was.
    QString error;
    if (!writeMovie(working_, model_->movie(), &error)) {
        status_->setText(tr("The movie cannot be written: %1").arg(error));
        return;
    }
    emulator_->reloadMovie();
    if (emulator_->movieFrame() > row) emulator_->seek(row);
    note_.clear();
    updateStatus();
}

QVector<int> TasEditorWindow::selectedRows() const {
    QVector<int> rows;
    for (const QModelIndex& index : view_->selectionModel()->selectedRows()) rows.append(index.row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

void TasEditorWindow::insertFrames(bool copy) {
    QVector<int> rows = selectedRows();
    if (rows.isEmpty()) rows = {std::clamp(model_->currentFrame(), 0, model_->rowCount())};
    QVector<MovieFrame> frames;
    for (const int row : rows) {
        frames.append(copy && row < model_->rowCount() ? model_->movie().frames.at(row) : MovieFrame());
    }
    // A copy goes after the frames copied, empty frames before the selection.
    const int at = copy ? rows.last() + 1 : rows.first();
    edit(std::min(at, model_->rowCount()), 0, frames, copy ? tr("Duplicate frames") : tr("Insert frames"));
}

void TasEditorWindow::removeFrames() {
    const QVector<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    undo_->beginMacro(tr("Delete frames"));
    // From the last, so the rows before stay where they are.
    for (int i = rows.size() - 1; i >= 0;) {
        int j = i;
        while (j > 0 && rows.at(j - 1) == rows.at(j) - 1) --j;
        edit(rows.at(j), rows.at(i) - rows.at(j) + 1, {}, tr("Delete frames"));
        i = j - 1;
    }
    undo_->endMacro();
}

void TasEditorWindow::save() {
    QString error;
    if (!writeMovie(origin_, model_->movie(), &error)) {
        status_->setText(tr("The movie cannot be written: %1").arg(error));
        return;
    }
    undo_->setClean();
    updateStatus();
}

void TasEditorWindow::followEmulator() {
    model_->setCurrentFrame(emulator_->movieFrame());
    if (follow_->isChecked() && model_->currentFrame() > 0 && model_->currentFrame() <= model_->rowCount()) {
        view_->scrollTo(model_->index(model_->currentFrame() - 1, 0), QAbstractItemView::PositionAtCenter);
    }
    updateStatus();
}

void TasEditorWindow::onEmulationEvent(const QString& event) {
    if (event.startsWith(QLatin1String("stopped-"))) {
        const int index = event.mid(8).toInt() - 1;
        note_ = tr("stopped: %1 at frame %2").arg(stops_->itemText(index)).arg(emulator_->movieFrame());
        updateStatus();
        const int row = emulator_->movieFrame() - 1;
        if (row >= 0 && row < model_->rowCount()) {
            view_->selectRow(row);
            view_->scrollTo(model_->index(row, 0), QAbstractItemView::PositionAtCenter);
        }
        return;
    }
    if (event == QLatin1String("stopped")) {
        // A recording from here ended: its frames are in the working file.
        QString error;
        EditedMovie movie;
        if (readMovie(working_, &movie, &error) && movie.frames.size() != model_->rowCount()) {
            undo_->clear();
            model_->setMovie(movie);
            updateStatus();
        }
    }
}

void TasEditorWindow::updateStatus() {
    QString text = tr("Frame %1 of %2").arg(model_->currentFrame()).arg(model_->rowCount());
    if (!undo_->isClean()) text += QStringLiteral(" · ") + tr("not saved");
    if (!note_.isEmpty()) text += QStringLiteral(" · ") + note_;
    status_->setText(text);
}

void TasEditorWindow::keyPressEvent(QKeyEvent* event) {
    const bool control = event->modifiers() & Qt::ControlModifier;
    if (event->key() == Qt::Key_Insert) {
        insertFrames(control);
    } else if (event->key() == Qt::Key_Delete) {
        removeFrames();
    } else if (control && event->key() == Qt::Key_Z) {
        undo_->undo();
    } else if (control && event->key() == Qt::Key_Y) {
        undo_->redo();
    } else if (control && event->key() == Qt::Key_S) {
        save();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QVector<int> rows = selectedRows();
        if (!rows.isEmpty()) emulator_->seek(rows.first() + 1);
    } else {
        StudioDialog::keyPressEvent(event);
    }
}

}  // namespace rl::shell
