#include "shell/tas_editor.hpp"

#include "runtime/emulator_runner.hpp"
#include "sdk/i18n.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShowEvent>
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

namespace {

// The movie's `source` header value, empty without one.
QString movieSource(const EditedMovie& movie) {
    for (const QString& line : movie.header) {
        if (line.startsWith(QLatin1String("source "))) return line.mid(7).trimmed();
    }
    return QString();
}

// A recording made in MAME, which the kit plays frame for frame (another
// emulator's is lined up with MAME's frames first).
bool isMameMovie(const EditedMovie& movie) {
    return movieSource(movie).section(QLatin1Char(' '), 0, 0) == QLatin1String("mame");
}

// The movie marked as MAME's own frames, ported from the source it had (the
// kit keeps that source's power-on work RAM).
void markAsMame(EditedMovie* movie) {
    const QString source = movieSource(*movie);
    const QString marked = source.isEmpty() ? QStringLiteral("source mame")
                                            : QStringLiteral("source mame from ") + source;
    for (QString& line : movie->header) {
        if (line.startsWith(QLatin1String("source "))) {
            line = marked;
            return;
        }
    }
    const int platform = movie->header.indexOf(QRegularExpression(QStringLiteral("^platform ")));
    movie->header.insert(platform + 1, marked);
}

}  // namespace

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

void MovieTableView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        emit playPauseRequested();
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}

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
    // The frame pressed is the one selected; the edit then shows it.
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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

}  // namespace

TasEditorWindow::TasEditorWindow(runtime::EmulatorRunner* emulator, QWidget* parent)
    : QFrame(parent), emulator_(emulator), model_(new MovieTableModel(this)),
      view_(new MovieTableView(this)), undo_(new QUndoStack(this)) {
    setProperty("surface", QStringLiteral("panel"));
    setMinimumWidth(360);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 12);
    layout->setSpacing(8);

    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setShowGrid(true);
    view_->setWordWrap(false);
    view_->setFont(theme::font(theme::TextRole::Body, 12.0));
    // Frame numbers in a fixed-width font, with room above and below them.
    view_->verticalHeader()->setFont(theme::font(theme::TextRole::Mono, 11.0));
    view_->verticalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    view_->verticalHeader()->setMinimumSectionSize(24);
    view_->verticalHeader()->setDefaultSectionSize(24);
    view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view_->horizontalHeader()->setHighlightSections(false);
    layout->addWidget(view_, 1);
    setFocusProxy(view_);

    status_ = new QLabel(this);
    status_->setFont(theme::font(theme::TextRole::Body, 12.0));
    layout->addWidget(status_);

    connect(view_, &MovieTableView::painted, this,
            [this](int row, const QVector<MovieFrame>& before, const QVector<MovieFrame>& after) {
                undo_->push(new SpliceCommand(model_, row, before, after, true, [this](int r) { applied(r); },
                                              tr("Change buttons")));
            });
    connect(view_, &MovieTableView::playPauseRequested, this, &TasEditorWindow::playOrPause);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &TasEditorWindow::onSelection);
    connect(undo_, &QUndoStack::indexChanged, this, &TasEditorWindow::changed);
    connect(undo_, &QUndoStack::cleanChanged, this, [this] {
        updateStatus();
        emit changed();
    });
    connect(emulator_, &runtime::EmulatorRunner::emulationChanged, this, &TasEditorWindow::followEmulator);
    connect(emulator_, &runtime::EmulatorRunner::emulationEvent, this, [this](const QString& event) {
        // Paused at the movie's end: that frame is selected.
        if (event == QLatin1String("movie-end")) {
            followPause_ = true;
            followEmulator();
        }
    });
}

bool TasEditorWindow::open(const QString& working, const QString& origin, QString* error) {
    const QString from = origin.isEmpty() ? working : origin;
    // The same run opened again: its edits not saved stay, with their undo.
    const bool same = working == working_ && from == origin_ && isModified();
    if (!same) {
        EditedMovie movie;
        if (!readMovie(working, &movie, error)) return false;
        working_ = working;
        origin_ = from;
        // The working file holds the frames as MAME plays them. The kit wrote
        // it from another emulator's movie, or from one it lines up (frames
        // dropped, an offset): those frames are not the origin's, and Save
        // puts them in a MAME movie of its own instead.
        toMame_ = working_ != origin_ && !isMameMovie(movie);
        EditedMovie original;
        QString ignored;
        askPath_ = working_ != origin_ &&
                   (toMame_ || QFileInfo(origin_).suffix().compare(QLatin1String("rlmovie"), Qt::CaseInsensitive) != 0 ||
                    !readMovie(origin_, &original, &ignored) || original.offset != 0);
        undo_->clear();
        model_->setMovie(movie);
    }
    emulator_->setMovieEditing(true);
    // The frame the game shows is the one selected.
    followPause_ = false;
    if (model_->rowCount() > 0) selectRow(std::clamp(emulator_->movieFrame() - 1, 0, model_->rowCount() - 1));
    followEmulator();
    emit changed();
    return true;
}

void TasEditorWindow::showEvent(QShowEvent* event) {
    QFrame::showEvent(event);
    if (!event->spontaneous()) emulator_->setMovieEditing(true);
    emit changed();
}

void TasEditorWindow::hideEvent(QHideEvent* event) {
    QFrame::hideEvent(event);
    if (!event->spontaneous()) emulator_->setMovieEditing(false);
    emit changed();
}

void TasEditorWindow::edit(int row, int count, const QVector<MovieFrame>& frames, const QString& text) {
    undo_->push(new SpliceCommand(model_, row, model_->movie().frames.mid(row, count), frames, false,
                                  [this](int r) { applied(r); }, text));
}

void TasEditorWindow::applied(int row) {
    // The run plays the working file: write it and let the run take it in.
    QString error;
    if (!writeMovie(working_, model_->movie(), &error)) {
        status_->setText(tr("The movie cannot be written: %1").arg(error));
        return;
    }
    // Row r is frame r + 1: the states from that frame on show the old input.
    emulator_->reloadMovie(row + 1);
    updateStatus();
    if (model_->rowCount() == 0) return;
    // The frame edited is selected and shown as the edit makes it: from a
    // state before the edit when the game shows a frame after it.
    const int shown = std::clamp(row, 0, model_->rowCount() - 1);
    selectRow(shown);
    followPause_ = false;
    if (emulator_->movieFrame() > row) {
        emulator_->replay(shown + 1);
    } else {
        emulator_->seek(shown + 1);
    }
}

void TasEditorWindow::selectRow(int row) {
    if (row < 0 || row >= model_->rowCount()) return;
    quiet_ = true;
    view_->selectRow(row);
    quiet_ = false;
    view_->scrollTo(model_->index(row, 0));
}

int TasEditorWindow::selectedRow() const {
    const QModelIndexList rows = view_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void TasEditorWindow::onSelection() {
    emit changed();
    // A frame the user selects is shown; a press on a button shows it
    // after its edit.
    if (quiet_ || view_->isPainting()) return;
    const int row = selectedRow();
    if (row < 0) return;
    followPause_ = false;
    emulator_->seek(row + 1);
}

void TasEditorWindow::copy() {
    const int row = selectedRow();
    if (row < 0) return;
    copied_ = model_->movie().frames.at(row);
    hasCopy_ = true;
    emit changed();
}

void TasEditorWindow::cut() {
    const int row = selectedRow();
    if (row < 0) return;
    copy();
    edit(row, 1, {}, tr("Cut frame"));
}

void TasEditorWindow::paste() {
    if (!hasCopy_) return;
    const int row = selectedRow();
    edit(row < 0 ? model_->rowCount() : row, 0, {copied_}, tr("Paste frame"));
}

void TasEditorWindow::insertFrame() {
    const int row = selectedRow();
    edit(row < 0 ? model_->rowCount() : row, 0, {MovieFrame()}, tr("Insert frame"));
}

void TasEditorWindow::deleteFrame() {
    const int row = selectedRow();
    if (row >= 0) edit(row, 1, {}, tr("Delete frame"));
}

void TasEditorWindow::playOrPause() {
    if (!emulator_->isPaused()) {
        // Paused where it is: that frame is selected once the game says
        // where it stopped.
        followPause_ = true;
        emulator_->togglePause();
        return;
    }
    followPause_ = false;
    // Row r is frame r + 1, the frame shown: the movie plays on after it.
    const int row = selectedRow();
    emulator_->playFrom(row >= 0 ? row + 1 : emulator_->movieFrame());
}

void TasEditorWindow::stop() {
    followPause_ = false;
    if (!emulator_->isPaused()) emulator_->togglePause();
    const int row = selectedRow();
    if (row >= 0) emulator_->seek(row + 1);
}

bool TasEditorWindow::isModified() const {
    return !undo_->isClean();
}

bool TasEditorWindow::save() {
    QString target = origin_;
    if (askPath_) {
        const QFileInfo info(origin_);
        target = QFileDialog::getSaveFileName(
            this, tr("Save the movie as MAME plays it"),
            info.dir().filePath(info.completeBaseName() + QStringLiteral("-mame.rlmovie")),
            tr("Retrolarium movies (*.rlmovie)"));
        if (target.isEmpty()) return false;
    }
    EditedMovie movie = model_->movie();
    if (toMame_) markAsMame(&movie);
    QString error;
    if (!writeMovie(target, movie, &error)) {
        status_->setText(tr("The movie cannot be written: %1").arg(error));
        return false;
    }
    origin_ = target;
    askPath_ = false;
    undo_->setClean();
    updateStatus();
    return true;
}

void TasEditorWindow::discard() {
    undo_->clear();
    updateStatus();
}

void TasEditorWindow::followEmulator() {
    model_->setCurrentFrame(emulator_->movieFrame());
    const int frame = model_->currentFrame();
    if (frame > 0 && frame <= model_->rowCount()) {
        if (emulator_->isPaused()) {
            if (followPause_) selectRow(frame - 1);
        } else if (frame != followed_) {
            // Played, the frame stays in view.
            view_->scrollTo(model_->index(frame - 1, 0));
        }
    }
    followed_ = frame;
    updateStatus();
}

void TasEditorWindow::updateStatus() {
    QString text = QFileInfo(origin_).fileName() + QStringLiteral(" · ") +
                   tr("Frame %1 of %2").arg(model_->currentFrame()).arg(model_->rowCount());
    if (!undo_->isClean()) text += QStringLiteral(" · ") + tr("not saved");
    status_->setText(text);
}

}  // namespace rl::shell
