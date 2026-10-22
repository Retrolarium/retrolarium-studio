#include "widgets/collapsible_section.hpp"

#include "theme/icons.hpp"
#include "theme/theme.hpp"
#include "theme/typography.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace rl::ui {

class CollapsibleSection::Header final : public QWidget {
public:
    Header(const QString& title, CollapsibleSection* owner) : QWidget(owner), owner_(owner), title_(title) {
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setTag(const QString& tag) {
        tag_ = tag;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            owner_->setExpanded(!owner_->isExpanded());
        }
    }

    void enterEvent(QEvent*) override {
        hovered_ = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        hovered_ = false;
        update();
    }

    void paintEvent(QPaintEvent*) override {
        const theme::Tokens& t = theme::current();
        QPainter painter(this);
        if (hovered_) {
            painter.fillRect(rect(), t.hover);
        }
        const int left = 14;
        const QString chevron = owner_->isExpanded() ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right");
        painter.drawPixmap(left, (height() - 10) / 2, theme::iconPixmap(chevron, 10, t.faint, devicePixelRatioF()));
        painter.setPen(t.faint);
        painter.setFont(theme::font(theme::TextRole::Caps, theme::size::kCaps));
        painter.drawText(QRect(left + 18, 0, width() - left - 18, height()), Qt::AlignVCenter, title_);
        if (!tag_.isEmpty()) {
            painter.setFont(theme::font(theme::TextRole::Mono, 10.5));
            painter.drawText(rect().adjusted(0, 0, -14, 0), Qt::AlignVCenter | Qt::AlignRight, tag_);
        }
    }

private:
    CollapsibleSection* owner_;
    QString title_;
    QString tag_;
    bool hovered_ = false;
};

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* body, QWidget* parent)
    : QWidget(parent), header_(new Header(title, this)), body_(body) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    frame_ = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(frame_);
    bodyLayout->setContentsMargins(14, 2, 14, 14);
    bodyLayout->addWidget(body_);
    layout->addWidget(frame_);
    frame_->setVisible(false);
    connect(&theme::Theme::instance(), &theme::Theme::changed, header_, [this] { header_->update(); });
}

void CollapsibleSection::setTag(const QString& tag) { header_->setTag(tag); }

void CollapsibleSection::setExpanded(bool expanded) {
    if (expanded == expanded_) {
        return;
    }
    expanded_ = expanded;
    frame_->setVisible(expanded_);
    header_->update();
    emit expandedChanged(expanded_);
}

}  // namespace rl::ui
