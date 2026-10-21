#pragma once

#include <QAbstractTableModel>
#include <QFrame>
#include <QTableView>
#include <QVector>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSpinBox;
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

// Run > Movie > TAS editor, docked beside the Playtest picture: the movie
// that plays, frame by frame. A frame
// clicked is shown in the game (the run goes back or forward to it, from the
// nearest state it kept); Play from here plays on from the selected frame,
// and Pause comes back to it; an edit is saved to the movie at once, the
// states from the edited frame on are dropped and the game goes back to the
// frame before it; a run goes on to the game's stop condition (a death) or the
// end. Frames are inserted empty, as copies, or with chosen buttons held.
class TasEditorWindow final : public QFrame {
    Q_OBJECT

public:
    TasEditorWindow(runtime::EmulatorRunner* emulator, QWidget* parent = nullptr);

signals:
    // The close button: the host takes the editor out of the view.
    void closeRequested();

public:
    // The movie the run plays (its working file) and the one it came from,
    // which Save writes. Opened again on the same files with edits not saved,
    // the editor keeps them.
    bool open(const QString& working, const QString& origin, QString* error);
    QString origin() const { return origin_; }
    // Edits not saved to the origin yet.
    bool isModified() const;
    // Writes the edits to the origin: to a MAME movie of its own (asked for)
    // when the origin is another emulator's or the kit lines it up. False
    // when it was not written.
    bool save();
    // Forgets the edits not saved (the run is going away).
    void discard();

protected:
    // The editor's keys (Ctrl+Z, Ctrl+S, Pause...) are the window's
    // shortcuts too: in the editor they are the editor's.
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void edit(int row, int count, const QVector<MovieFrame>& frames, const QString& text);
    void applied(int row);
    void insertFrames(bool copy);
    void insertHeld();
    void removeFrames();
    void playFromSelection();
    void pause();
    void selectFrameShown();
    // The toggles of Insert holding, one per button of the movie's pad.
    void buildHoldButtons();
    QVector<int> selectedRows() const;
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
    QSpinBox* holdCount_;
    QHBoxLayout* holdRow_;
    QVector<QPushButton*> holdButtons_;
    int playedFrom_ = -1;   // the row Play from here started at
    int followed_ = -1;     // the frame Follow last brought into view
    bool wasPaused_ = true;
    QString working_;
    QString origin_;
    bool askPath_ = false;   // Save asks for a new file: the origin cannot take MAME's frames
    bool toMame_ = false;    // the frames saved are marked as MAME's own (no lining up)
    QString note_;          // what the last run to a stop found
};

}  // namespace rl::shell
