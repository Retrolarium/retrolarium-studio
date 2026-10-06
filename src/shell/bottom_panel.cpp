#include "shell/bottom_panel.hpp"

#include "sdk/i18n.hpp"
#include "theme/icons.hpp"
#include "theme/paint.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"
#include "widgets/label.hpp"
#include "widgets/panel_tabs.hpp"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace rl::shell {

namespace {

constexpr int kBodyHeight = 112;
constexpr int kStripHeight = 28;

// Severity mark: an accent square, filled for warnings, solid text color for
// errors.
class SeverityMark final : public QWidget {
public:
    SeverityMark(sdk::Severity severity, QWidget* parent) : QWidget(parent), severity_(severity) {
        setFixedSize(7, 7);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (severity_ == sdk::Severity::Error) {
            painter.fillRect(rect(), t.text);
            return;
        }
        if (severity_ == sdk::Severity::Warning) {
            painter.fillRect(rect(), t.accent);
        }
        theme::paint::hairlineRect(painter, rect(), t.accent);
    }

private:
    sdk::Severity severity_;
};

QString severityName(sdk::Severity severity) {
    switch (severity) {
    case sdk::Severity::Error:
        return rlTr("studio", "Error");
    case sdk::Severity::Warning:
        return rlTr("studio", "Warning");
    case sdk::Severity::Info:
        break;
    }
    return rlTr("studio", "Info");
}

class MessageRow final : public QWidget {
public:
    MessageRow(const sdk::Diagnostic& d, QWidget* parent) : QWidget(parent) {
        setFixedHeight(26);
        setAttribute(Qt::WA_Hover);
        setCursor(d.kind.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 12, 0);
        layout->setSpacing(0);
        auto* severity = new QWidget(this);
        severity->setFixedWidth(84);
        auto* sevLayout = new QHBoxLayout(severity);
        sevLayout->setContentsMargins(0, 0, 0, 0);
        sevLayout->setSpacing(7);
        sevLayout->addWidget(new SeverityMark(d.severity, severity));
        auto* name = new ui::Label(severityName(d.severity), ui::Label::Role::Row, severity);
        name->setTone(QStringLiteral("muted"));
        sevLayout->addWidget(name);
        sevLayout->addStretch(1);
        layout->addWidget(severity);
        auto* source = new ui::Label(d.source, ui::Label::Role::Mono, this);
        source->setTone(QStringLiteral("muted"));
        source->setFixedWidth(160);
        layout->addWidget(source);
        auto* message = new ui::Label(d.message, ui::Label::Role::Row, this);
        message->setToolTip(d.message);
        layout->addWidget(message, 1);
        auto* location = new ui::Label(d.location, ui::Label::Role::Mono, this);
        location->setTone(QStringLiteral("accent"));
        location->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        location->setMinimumWidth(160);
        layout->addWidget(location);
    }

    std::function<void()> onActivate;

protected:
    void enterEvent(QEvent*) override {
        hovered_ = true;
        update();
    }
    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onActivate) {
            onActivate();
        }
    }
    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (hovered_) {
            painter.fillRect(rect(), t.hover);
        }
        painter.fillRect(QRect(0, height() - 1, width(), 1), t.line);
    }

private:
    bool hovered_ = false;
};

}  // namespace

BottomPanel::BottomPanel(QWidget* parent) : QFrame(parent) {
    setProperty("surface", QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Strong, this));

    auto* strip = new QWidget(this);
    strip->setFixedHeight(kStripHeight);
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(6, 0, 10, 0);
    stripLayout->setSpacing(2);
    chevron_ = new QToolButton(strip);
    chevron_->setAutoRaise(true);
    chevron_->setFixedSize(20, 20);
    chevron_->setIconSize(QSize(10, 10));
    chevron_->setToolTip(tr("Show or hide the panel (Ctrl+J)"));
    connect(chevron_, &QToolButton::clicked, this, [this] { setExpanded(!expanded_); });
    stripLayout->addWidget(chevron_);
    tabs_ = new ui::PanelTabs(strip);
    tabs_->addTab(tr("Messages"), QStringLiteral("0"));
    tabs_->addTab(tr("Build output"));
    stripLayout->addWidget(tabs_);
    stripLayout->addStretch(1);
    summary_ = new ui::Label(QString(), ui::Label::Role::Small, strip);
    summary_->setTone(QStringLiteral("faint"));
    stripLayout->addWidget(summary_);
    layout->addWidget(strip);
    layout->addWidget(new ui::Divider(Qt::Horizontal, ui::Divider::Strength::Line, this));

    stack_ = new QStackedWidget(this);
    stack_->setFixedHeight(kBodyHeight);
    auto* scroll = new QScrollArea(stack_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    messages_ = new QWidget(scroll);
    messageLayout_ = new QVBoxLayout(messages_);
    messageLayout_->setContentsMargins(0, 0, 0, 0);
    messageLayout_->setSpacing(0);
    messageLayout_->addStretch(1);
    scroll->setWidget(messages_);
    stack_->addWidget(scroll);

    log_ = new QPlainTextEdit(stack_);
    log_->setProperty("log", true);
    log_->setReadOnly(true);
    log_->setFont(theme::font(theme::TextRole::Mono, 11.5));
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    stack_->addWidget(log_);
    layout->addWidget(stack_);

    connect(tabs_, &ui::PanelTabs::currentChanged, this, [this](int index) {
        stack_->setCurrentIndex(index);
        setExpanded(true);
    });
    connect(tabs_, &ui::PanelTabs::currentClicked, this, [this] { setExpanded(!expanded_); });
    connect(&theme::Theme::instance(), &theme::Theme::changed, this, [this] { updateChevron(); });
    updateChevron();
    setDiagnostics({});
}

void BottomPanel::updateChevron() {
    chevron_->setIcon(theme::icon(expanded_ ? QStringLiteral("chevron-down")
                                            : QStringLiteral("chevron-right"),
                                  theme::IconTone::Faint));
}

void BottomPanel::setExpanded(bool expanded) {
    expanded_ = expanded;
    stack_->setVisible(expanded);
    updateChevron();
}

void BottomPanel::showMessages() {
    tabs_->setCurrentIndex(0);
    setExpanded(true);
}

void BottomPanel::showBuildOutput() {
    tabs_->setCurrentIndex(1);
    setExpanded(true);
}

QString BottomPanel::summary() const {
    int errors = 0;
    int warnings = 0;
    for (const sdk::Diagnostic& d : diagnostics_) {
        errors += d.severity == sdk::Severity::Error;
        warnings += d.severity == sdk::Severity::Warning;
    }
    // Whole phrases per count, so a translation can word each its own way.
    return QStringLiteral("%1 · %2")
        .arg(errors == 1 ? tr("1 error") : tr("%1 errors").arg(errors),
             warnings == 1 ? tr("1 warning") : tr("%1 warnings").arg(warnings));
}

void BottomPanel::setDiagnostics(const QVector<sdk::Diagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    while (messageLayout_->count() > 1) {
        QLayoutItem* item = messageLayout_->takeAt(0);
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    int index = 0;
    for (const sdk::Diagnostic& d : diagnostics_) {
        auto* row = new MessageRow(d, messages_);
        row->onActivate = [this, d] { emit diagnosticActivated(d); };
        messageLayout_->insertWidget(index++, row);
    }
    tabs_->setCount(0, QString::number(diagnostics_.size()));
    summary_->setText(summary());
    emit summaryChanged(summary());
}

void BottomPanel::appendLog(const QString& line) {
    log_->appendPlainText(line);
    ++logLines_;
    tabs_->setCount(1, QString::number(logLines_));
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void BottomPanel::clearLog() {
    log_->clear();
    logLines_ = 0;
    tabs_->setCount(1, QString());
}

}  // namespace rl::shell
