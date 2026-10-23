#include "editors/emulator_view.hpp"

#include "runtime/emulator_runner.hpp"
#include "runtime/process_runner.hpp"
#include "sdk/editor_model.hpp"
#include "sdk/i18n.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/view_chip.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace rl::editors {

namespace paint = theme::paint;

namespace {

// "Building, starting, stopping": a small card with a spinner over the
// picture. It is a native window so it sits above the emulator's own.
class BusyCard final : public QWidget {
public:
    explicit BusyCard(QWidget* parent) : QWidget(parent), timer_(new QTimer(this)) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFixedSize(300, 66);
        timer_->setInterval(30);
        connect(timer_, &QTimer::timeout, this, [this] {
            angle_ = (angle_ + 12) % 360;
            update();
        });
        hide();
    }

    void showBusy(const QString& title, const QString& detail) {
        title_ = title;
        detail_ = detail;
        if (!isVisible()) {
            show();
            raise();
            timer_->start();
        }
        update();
    }

    void showIdle() {
        timer_->stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.panel);
        paint::hairlineRect(painter, rect(), t.lineStrong);
        painter.fillRect(QRect(0, 0, 3, height()), t.accent);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF ring(20, height() / 2.0 - 11, 22, 22);
        painter.setPen(QPen(t.line, 2.5));
        painter.drawEllipse(ring);
        QPen arc(t.accent, 2.5);
        arc.setCapStyle(Qt::FlatCap);
        painter.setPen(arc);
        painter.drawArc(ring, -angle_ * 16, 100 * 16);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const int left = 58;
        const int room = width() - left - 12;
        painter.setFont(theme::font(theme::TextRole::BodyMedium, 13.5));
        painter.setPen(t.text);
        painter.drawText(QRect(left, 12, room, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(title_, Qt::ElideRight, room));
        painter.setFont(theme::font(theme::TextRole::Mono, 11.0));
        painter.setPen(t.muted);
        painter.drawText(QRect(left, 34, room, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(detail_, Qt::ElideRight, room));
    }

private:
    QTimer* timer_;
    QString title_;
    QString detail_;
    int angle_ = 0;
};

// Lies over the emulator's picture: black while the game starts, so the
// boot and the steering to the start point stay unseen, and the frozen frame
// in black and white while it is paused (a paused emulator cannot draw).
// Native for the same reason as BusyCard.
class ScreenCover final : public QWidget {
public:
    explicit ScreenCover(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    bool showsPause() const { return isVisible() && paused_; }

    void showBlack() {
        paused_ = false;
        picture_ = QImage();
        show();
        raise();
        update();
    }

    void showPaused(const QImage& picture) {
        paused_ = true;
        picture_ = picture.convertToFormat(QImage::Format_Grayscale8);
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (!paused_) {
            return;
        }
        if (!picture_.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(rect(), picture_);
        }
        painter.setFont(theme::font(theme::TextRole::Caps, 11.0));
        const QString tag = rlTr("studio", "PAUSED");
        const QRect box(10, 10, painter.fontMetrics().horizontalAdvance(tag) + 14, 20);
        painter.fillRect(box, theme::canvas::kTagFill);
        painter.setPen(theme::canvas::kTagText);
        painter.drawText(box, Qt::AlignCenter, tag);
    }

private:
    QImage picture_;
    bool paused_ = false;
};

// A blinking red dot and REC in the picture's corner while the game records
// the player's input. Native to lie over the emulator surface.
class RecordBadge final : public QWidget {
public:
    explicit RecordBadge(QWidget* parent) : QWidget(parent), blink_(new QTimer(this)) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(66, 24);
        blink_->setInterval(500);
        QObject::connect(blink_, &QTimer::timeout, this, [this] {
            lit_ = !lit_;
            update();
        });
        hide();
    }

    void setRecording(bool recording) {
        if (recording == isVisible()) return;
        if (recording) {
            lit_ = true;
            blink_->start();
            show();
            raise();
        } else {
            blink_->stop();
            hide();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0, 0, 0));
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor red(0xff, 0x30, 0x30);
        if (lit_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(red);
            painter.drawEllipse(QRectF(8, 6, 12, 12));
        }
        painter.setFont(theme::font(theme::TextRole::Caps, 11.0));
        painter.setPen(red);
        painter.drawText(rect().adjusted(26, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("REC"));
    }

private:
    QTimer* blink_;
    bool lit_ = true;
};

}  // namespace

// The canvas ground with the emulator surface fitted at the screen aspect.
class EmulatorStage final : public QWidget {
public:
    EmulatorStage(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner, QWidget* parent)
        : QWidget(parent),
          model_(model),
          runner_(runner),
          surface_(new runtime::EmulatorSurface(this)),
          cover_(new ScreenCover(this)),
          record_(new RecordBadge(this)),
          busy_(new BusyCard(this)) {
        setMinimumHeight(240);
        surface_->hide();
        runner_->setSurface(surface_);
    }

    runtime::EmulatorSurface* surface() const { return surface_; }

    void refresh() {
        surface_->setVisible(runner_->isRunning());
        layoutSurface();
        // The keyboard goes to the game as soon as it is up.
        const auto phase = runner_->phase();
        if (phase == runtime::EmulatorRunner::Phase::Running && lastPhase_ != phase && isVisible()) {
            surface_->setFocus(Qt::OtherFocusReason);
        }
        lastPhase_ = phase;
        if (runner_->isPaused()) {
            if (!cover_->showsPause()) {
                cover_->showPaused(runner_->picture());  // the frame taken as it froze
            }
        } else if (runner_->phase() == runtime::EmulatorRunner::Phase::Starting && runner_->hidesStart()) {
            cover_->showBlack();
        } else {
            cover_->hide();
        }
        record_->setRecording(runner_->isRunning() && model_->isRecording());
        record_->raise();
        busy_->raise();
        update();
    }

    // An empty title hides the card.
    void setBusy(const QString& title, const QString& detail) {
        if (title.isEmpty()) {
            busy_->showIdle();
        } else {
            busy_->showBusy(title, detail);
            layoutSurface();
        }
        update();
    }

protected:
    void resizeEvent(QResizeEvent*) override { layoutSurface(); }

    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        painter.fillRect(rect(), t.canvas);
        const QRect screen = screenRect();
        paint::cornerMarks(painter, screen, 13, 7, theme::canvas::kMark, paint::kDiagonal);
        if (runner_->isRunning()) {
            return;
        }
        paint::hatch(painter, screen, QColor(255, 255, 255, 9), 20.0, 10.0);
        paint::hairlineRect(painter, screen, QColor(255, 255, 255, 38));
        if (busy_->isVisible()) {
            return;  // the card says what is happening
        }
        painter.setFont(theme::font(theme::TextRole::Mono, 11.5));
        painter.setPen(QColor(255, 255, 255, 140));
        const QSize native = model_->screenSize();
        painter.drawText(screen, Qt::AlignCenter,
                         rlTr("studio",
                              "Play from selected runs %1 · Play starts from power-on\n"
                              "%2 · %3 × %4 · aspect-fit")
                             .arg(model_->startLabel(), model_->coreLabel())
                             .arg(native.width())
                             .arg(native.height()));
    }

private:
    QRect screenRect() const {
        // Mega Drive and NES pictures are shown at 4:3 like a television.
        const QRect room = rect().adjusted(28, 28, -28, -28);
        const QSize fitted = QSize(4, 3).scaled(room.size(), Qt::KeepAspectRatio);
        return QRect(room.left() + (room.width() - fitted.width()) / 2,
                     room.top() + (room.height() - fitted.height()) / 2, fitted.width(), fitted.height());
    }

    void layoutSurface() {
        const QRect screen = screenRect();
        surface_->setGeometry(screen);
        cover_->setGeometry(screen);
        record_->move(screen.right() - record_->width() - 10, screen.top() + 10);
        busy_->move(screen.center() - QPoint(busy_->width() / 2, busy_->height() / 2));
        busy_->raise();
    }

    sdk::EmulatorModel* model_;
    runtime::EmulatorRunner* runner_;
    runtime::EmulatorSurface* surface_;
    ScreenCover* cover_;
    RecordBadge* record_;
    runtime::EmulatorRunner::Phase lastPhase_ = runtime::EmulatorRunner::Phase::Idle;
    BusyCard* busy_;
};

EmulatorView::EmulatorView(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner,
                           runtime::ProcessRunner* processes, QWidget* parent)
    : EditorView(model, parent), emulator_(model), runner_(runner), processes_(processes) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setChildrenCollapsible(false);
    stage_ = new EmulatorStage(model, runner, split_);
    split_->addWidget(stage_);
    layout->addWidget(split_, 1);

    connect(runner_, &runtime::EmulatorRunner::stateChanged, this, &EmulatorView::updateState);
    if (processes_) {
        connect(processes_, &runtime::ProcessRunner::started, this, [this](const QString& label, const QString&) {
            busyStep_ = label;
            updateState();
        });
        connect(processes_, &runtime::ProcessRunner::finished, this, [this] {
            busyStep_.clear();
            updateState();
        });
        if (processes_->isBusy()) {
            busyStep_ = tr("Working");
        }
    }
    // Focus only changes the status text (see createToolbar): a new toolbar
    // here would take the button being clicked away mid-click.
    connect(emulator_, &sdk::EditorModel::contentChanged, stage_, [this] { stage_->refresh(); });
    connect(emulator_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    updateState();
}

EmulatorView::~EmulatorView() {
    setSidePanel(nullptr);
    runner_->stopNow();
    runner_->setSurface(nullptr);
}

void EmulatorView::setSidePanel(QWidget* panel) {
    if (side_ == panel) return;
    if (side_) {
        side_->hide();
        side_->setParent(window());
    }
    side_ = panel;
    if (!panel) return;
    split_->insertWidget(0, panel);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    split_->setSizes({std::max(520, panel->sizeHint().width()), std::max(1, width() - 520)});
    panel->show();
}

void EmulatorView::updateState() {
    stage_->refresh();
    using Phase = runtime::EmulatorRunner::Phase;
    switch (runner_->phase()) {
        case Phase::Starting:
            stage_->setBusy(runner_->isReloading() ? tr("Reloading the game…") : tr("Starting the emulator…"),
                            runner_->title());
            break;
        case Phase::Stopping:
            stage_->setBusy(tr("Stopping…"), runner_->title());
            break;
        case Phase::Running:
        case Phase::Idle:
            stage_->setBusy(busyStep_.isEmpty() ? QString() : busyStep_ + QStringLiteral("…"), tr("Retrolarium Kit"));
            break;
    }
    emit toolbarChanged();
}

QWidget* EmulatorView::createToolbar(QWidget* parent) {
    auto* bar = new QWidget(parent);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    using Phase = runtime::EmulatorRunner::Phase;
    const Phase phase = runner_->phase();
    const bool running = phase == Phase::Running;
    // Nothing to press twice while a step is under way; the card says which.
    const bool settling = phase == Phase::Starting || phase == Phase::Stopping || !busyStep_.isEmpty();
    auto button = [bar](const QString& text, bool enabled) {
        auto* b = new QPushButton(text, bar);
        b->setFont(theme::font(theme::TextRole::Body, 12.0));
        b->setEnabled(enabled);
        b->setFocusPolicy(Qt::NoFocus);             // a click keeps the keyboard with the game
        b->setMinimumWidth(b->sizeHint().width());  // a narrow pane squeezes the status, not the labels
        return b;
    };
    // One label in every state, so the buttons after it never move.
    QPushButton* run = button(tr("Play from selected"), !settling);
    if (running) {
        run->setToolTip(tr("Rebuild and restart the game"));
    }
    connect(run, &QPushButton::clicked, this, [this] { emulator_->play(); });
    layout->addWidget(run);
    QPushButton* pause = button(runner_->isPaused() ? tr("Resume") : tr("Pause"), running);
    connect(pause, &QPushButton::clicked, this, [this] {
        if (!runner_->togglePause()) {
            emit actionFailed(tr("The emulator could not be paused."));
        }
    });
    layout->addWidget(pause);
    QPushButton* stop = button(tr("Stop"), running || phase == Phase::Starting);
    connect(stop, &QPushButton::clicked, this, [this] { runner_->stop(); });
    layout->addWidget(stop);
    QPushButton* shot = button(tr("Screenshot"), running);
    connect(shot, &QPushButton::clicked, this, [this] {
        QString error;
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Retrolarium");
        if (runner_->saveScreenshot(directory, &error).isEmpty()) {
            emit actionFailed(error);
        }
    });
    layout->addWidget(shot);
    auto* separator = new ui::Divider(Qt::Vertical, ui::Divider::Strength::Line, bar);
    separator->setFixedHeight(18);
    layout->addWidget(separator, 0, Qt::AlignVCenter);
    layout->addWidget(new ui::Label(tr("View"), ui::Label::Role::Caps, bar));
    for (const sdk::ViewOption& option : emulator_->viewOptions()) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { emulator_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    // How the picture shows, for every game: square pixels or the 4:3 TV
    // the machine assumes, and the studio's colours when the plugin gave
    // them. Remembered across runs.
    auto* pixels =
        new ui::ViewChip(tr("Pixels"), runner_->squarePixels() ? QStringLiteral("Square") : tr("TV 4:3"), bar);
    pixels->setChoices({QStringLiteral("Square"), tr("TV 4:3")});
    connect(pixels, &ui::ViewChip::valueChosen, this, [this](const QString& value) {
        runner_->setPicture(value == QLatin1String("Square"), runner_->studioColours());
        QSettings().setValue(QStringLiteral("playtest/squarePixels"), runner_->squarePixels());
    });
    layout->addWidget(pixels);
    auto* colours =
        new ui::ViewChip(tr("Colours"), runner_->studioColours() ? QStringLiteral("Studio") : tr("Emulator"), bar);
    colours->setChoices({QStringLiteral("Studio"), tr("Emulator")});
    connect(colours, &ui::ViewChip::valueChosen, this, [this](const QString& value) {
        runner_->setPicture(runner_->squarePixels(), value == QLatin1String("Studio"));
        QSettings().setValue(QStringLiteral("playtest/studioColours"), runner_->studioColours());
    });
    colours->setToolTip(tr("The colours the editors draw with, or the emulator's own"));
    layout->addWidget(colours);
    layout->addWidget(new ui::ViewChip(tr("Core"), emulator_->coreLabel(), bar));
    layout->addStretch(1);
    auto* state = new ui::Label(QString(), ui::Label::Role::Small, bar);
    const auto status = [this, state, phase, running] {
        QString text = tr("Idle");
        if (!busyStep_.isEmpty()) {
            text = busyStep_ + QStringLiteral("…");
        } else if (phase == Phase::Starting) {
            text = tr("Starting…");
        } else if (phase == Phase::Stopping) {
            text = tr("Stopping…");
        } else if (running) {
            text = runner_->isPaused() ? tr("Paused")
                   : stage_->surface()->hasFocus()
                       ? tr("Running · arrows, Z X, Enter, right Shift or a game pad · hold Tab to fast-forward")
                       : tr("Running · click the picture for the keyboard");
        }
        state->setText(text);
    };
    status();
    connect(stage_->surface(), &runtime::EmulatorSurface::focusChanged, state, status);
    layout->addWidget(state);
    // The movie, speed and state slot, kept current without a new toolbar.
    auto* emulation = new ui::Label(QString(), ui::Label::Role::Small, bar);
    emulation->setTone(QStringLiteral("accent"));
    layout->addWidget(emulation);
    const auto refresh = [this, emulation] {
        QStringList parts;
        const auto clock = [](int frames) {
            const int seconds = int(frames / 60.0 + 0.5);
            return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        };
        const QString mode = runner_->movieMode();
        if (runner_->isRunning() && mode == QLatin1String("record")) {
            parts << tr("● Recording movie %1 · frame %2").arg(clock(runner_->movieFrame())).arg(runner_->movieFrame());
        } else if (runner_->isRunning() && mode == QLatin1String("play")) {
            parts << tr("▶ Movie %1 / %2").arg(clock(runner_->movieFrame()), clock(runner_->movieLength()));
        }
        if (runner_->speed() != 100) {
            parts << (runner_->speed() == 0 ? tr("No speed limit") : tr("Speed %1%").arg(runner_->speed()));
        }
        if (runner_->hasControls() && runner_->isRunning()) {
            parts << tr("Slot %1").arg(runner_->stateSlot());
        }
        emulation->setText(parts.join(QStringLiteral(" · ")));
    };
    refresh();
    connect(runner_, &runtime::EmulatorRunner::emulationChanged, emulation, refresh);
    return bar;
}

}  // namespace rl::editors
