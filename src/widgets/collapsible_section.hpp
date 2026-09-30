#pragma once

#include <QWidget>

class QVBoxLayout;

namespace rl::ui {

// A 34px clickable header (chevron, uppercase title, optional mono tag on the
// right) above a body that shows and hides. Used for "Engine data".
class CollapsibleSection final : public QWidget {
    Q_OBJECT

public:
    CollapsibleSection(const QString& title, QWidget* body, QWidget* parent = nullptr);

    void setTag(const QString& tag);
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }

    QWidget* body() const { return body_; }

signals:
    void expandedChanged(bool expanded);

private:
    class Header;

    Header* header_;
    QWidget* body_;
    QWidget* frame_ = nullptr;  // padded holder of body_, the part that hides
    bool expanded_ = false;
};

}  // namespace rl::ui
