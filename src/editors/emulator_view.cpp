#include "editors/emulator_view.hpp"

#include "runtime/emulator_runner.hpp"
#include "runtime/process_runner.hpp"
#include "sdk/editor_model.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/view_chip.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
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

}  // namespace

// The canvas ground with the emulator surface fitted at the screen aspect.
class EmulatorStage final : public QWidget {
public:
    EmulatorStage(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner, QWidget* parent)
        : QWidget(parent), model_(model), runner_(runner), surface_(new runtime::EmulatorSurface(this)),
          busy_(new BusyCard(this)) {
        setMinimumHeight(240);
        surface_->hide();
        runner_->setSurface(surface_);
    }

    void refresh() {
        surface_->setVisible(runner_->isRunning());
        layoutSurface();
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
        painter.setFont(theme::font(theme::TextRole::Mono, 11.5));
        painter.setPen(QColor(255, 255, 255, 140));
        const QSize native = model_->screenSize();
        painter.drawText(screen, Qt::AlignCenter,
                         QStringLiteral("Press Play to build and run %1\n%2 · %3 × %4 · aspect-fit")
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
        busy_->move(screen.center() - QPoint(busy_->width() / 2, busy_->height() / 2));
        busy_->raise();
    }

    sdk::EmulatorModel* model_;
    runtime::EmulatorRunner* runner_;
    runtime::EmulatorSurface* surface_;
    BusyCard* busy_;
};

EmulatorView::EmulatorView(sdk::EmulatorModel* model, runtime::EmulatorRunner* runner,
                           runtime::ProcessRunner* processes, QWidget* parent)
    : EditorView(model, parent), emulator_(model), runner_(runner), processes_(processes) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    stage_ = new EmulatorStage(model, runner, this);
    layout->addWidget(stage_, 1);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, this));
    log_ = new QPlainTextEdit(this);
    log_->setProperty("log", true);
    log_->setReadOnly(true);
    log_->setFont(theme::font(theme::TextRole::Mono, 11.5));
    log_->setFixedHeight(130);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setPlainText(runner_->log().join(QLatin1Char('\n')));
    layout->addWidget(log_);

    connect(runner_, &runtime::EmulatorRunner::stateChanged, this, &EmulatorView::updateState);
    connect(runner_, &runtime::EmulatorRunner::outputLine, this, [this](const QString& line) {
        log_->appendPlainText(line);
        log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    });
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
            busyStep_ = QStringLiteral("Working");
        }
    }
    connect(emulator_, &sdk::EditorModel::contentChanged, stage_, qOverload<>(&QWidget::update));
    connect(emulator_, &sdk::EditorModel::toolsChanged, this, &EditorView::toolbarChanged);
    updateState();
}

EmulatorView::~EmulatorView() {
    runner_->stopNow();
    runner_->setSurface(nullptr);
}

void EmulatorView::updateState() {
    stage_->refresh();
    using Phase = runtime::EmulatorRunner::Phase;
    switch (runner_->phase()) {
    case Phase::Starting:
        stage_->setBusy(QStringLiteral("Starting the emulator…"), runner_->title());
        break;
    case Phase::Stopping:
        stage_->setBusy(QStringLiteral("Stopping…"), runner_->title());
        break;
    case Phase::Running:
    case Phase::Idle:
        stage_->setBusy(busyStep_.isEmpty() ? QString() : busyStep_ + QStringLiteral("…"),
                        QStringLiteral("Retrolarium Kit"));
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
        return b;
    };
    // One label in every state, so the buttons after it never move.
    QPushButton* run = button(QStringLiteral("Build and run"), !settling);
    if (running) {
        run->setToolTip(QStringLiteral("Rebuild and restart the game"));
    }
    connect(run, &QPushButton::clicked, this, [this] { emulator_->play(); });
    layout->addWidget(run);
    QPushButton* pause = button(runner_->isPaused() ? QStringLiteral("Resume") : QStringLiteral("Pause"), running);
    connect(pause, &QPushButton::clicked, this, [this] {
        if (!runner_->togglePause()) {
            emit actionFailed(QStringLiteral("The emulator could not be paused."));
        }
    });
    layout->addWidget(pause);
    QPushButton* stop = button(QStringLiteral("Stop"), running || phase == Phase::Starting);
    connect(stop, &QPushButton::clicked, this, [this] { runner_->stop(); });
    layout->addWidget(stop);
    QPushButton* shot = button(QStringLiteral("Screenshot"), running);
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
    layout->addWidget(new ui::Label(QStringLiteral("View"), ui::Label::Role::Caps, bar));
    for (const sdk::ViewOption& option : emulator_->viewOptions()) {
        auto* chip = new ui::ViewChip(option.label, option.value, bar);
        const QString key = option.key;
        chip->setChoices(option.choices);
        connect(chip, &ui::ViewChip::valueChosen, this,
                [this, key](const QString& value) { emulator_->setViewOption(key, value); });
        layout->addWidget(chip);
    }
    layout->addWidget(new ui::ViewChip(QStringLiteral("Core"), emulator_->coreLabel(), bar));
    layout->addStretch(1);
    QString status = QStringLiteral("Idle");
    if (!busyStep_.isEmpty()) {
        status = busyStep_ + QStringLiteral("…");
    } else if (phase == Phase::Starting) {
        status = QStringLiteral("Starting…");
    } else if (phase == Phase::Stopping) {
        status = QStringLiteral("Stopping…");
    } else if (running) {
        status = runner_->isPaused() ? QStringLiteral("Paused")
                                     : QStringLiteral("Running · game pad live");
    }
    auto* state = new ui::Label(status, ui::Label::Role::Small, bar);
    layout->addWidget(state);
    return bar;
}

}  // namespace rl::editors
