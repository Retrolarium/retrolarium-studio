#pragma once

#include "frame/studio_dialog.hpp"

#include <QAbstractTableModel>
#include <QTableView>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QUndoStack;

namespace rl::runtime {
class EmulatorRunner;
}

namespace rl::shell {

// One frame of an input movie: the pads' masks (MAME's, as .rlmovie keeps
// them) and a command on that frame ("reset", "power" or none).
struct MovieFrame {
    quint8 pad1 = 0;
    quint8 pad2 = 0;
    QString command;

    bool operator==(const MovieFrame& other) const {
        return pad1 == other.pad1 && pad2 == other.pad2 && command == other.command;
    }
};

// An .rlmovie read for editing: its header lines (without the frame count
// and offset, which are written again) and its frames.
struct EditedMovie {
    QStringList header;
    int offset = 0;
    QVector<MovieFrame> frames;
    QString platform() const;
};

bool readMovie(const QString& path, EditedMovie* movie, QString* error);
bool writeMovie(const QString& path, const EditedMovie& movie, QString* error);

// The frames of a movie, a row each; a column per button of pad one.
class MovieTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit MovieTableModel(QObject* parent = nullptr);

    void setMovie(const EditedMovie& movie);
    const EditedMovie& movie() const { return movie_; }
    // Replaces `count` frames from `row` with `frames` (an edit and its undo).
    void splice(int row, int count, const QVector<MovieFrame>& frames);
    int buttonMask(int column) const;
    // The frame the emulator shows (1-based: the frames emulated so far).
    void setCurrentFrame(int frame);
    int currentFrame() const { return current_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Button {
        QString label;
        int mask;
    };
    EditedMovie movie_;
    QVector<Button> buttons_;
    int current_ = 0;
};

// The table: a press on a button cell turns it on or off, and dragging over
// more frames does the same to them, as one edit.
class MovieTableView final : public QTableView {
    Q_OBJECT

public:
    explicit MovieTableView(QWidget* parent = nullptr);

signals:
    // Frames from `row` on (`count` of them) take `frames` (one edit).
    void painted(int row, const QVector<MovieFrame>& before, const QVector<MovieFrame>& after);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void paintAt(const QModelIndex& index);

    bool painting_ = false;
    bool value_ = false;
    int column_ = -1;
    int first_ = -1;
    int last_ = -1;
    QVector<MovieFrame> before_;   // the frames as they were, from first_ on
};

// Run > Movie > TAS editor: the movie that plays, frame by frame. A frame
// clicked is shown in the game (the run goes back or forward to it); an edit
// is saved to the movie at once and the game goes back to the frame before
// it; a run goes on to the game's stop condition (a death) or the end.
class TasEditorWindow final : public frame::StudioDialog {
    Q_OBJECT

public:
    TasEditorWindow(runtime::EmulatorRunner* emulator, QWidget* parent = nullptr);

    // The movie the run plays (its working file) and the one it came from,
    // which Save writes.
    bool open(const QString& working, const QString& origin, QString* error);
    QString origin() const { return origin_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void edit(int row, int count, const QVector<MovieFrame>& frames, const QString& text);
    void applied(int row);
    void insertFrames(bool copy);
    void removeFrames();
    QVector<int> selectedRows() const;
    void save();
    void followEmulator();
    void onEmulationEvent(const QString& event);
    void updateStatus();

    runtime::EmulatorRunner* emulator_;
    MovieTableModel* model_;
    MovieTableView* view_;
    QUndoStack* undo_;
    QComboBox* stops_;
    QCheckBox* follow_;
    QLabel* status_;
    QString working_;
    QString origin_;
    QString note_;          // what the last run to a stop found
};

}  // namespace rl::shell
