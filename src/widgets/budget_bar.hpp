#pragma once

#include <QWidget>

namespace rl::ui {

// Thin fill bar for byte budgets and string lengths: accent on the strong
// line. Over budget the fill turns to text color and runs the full width.
class BudgetBar final : public QWidget {
    Q_OBJECT

public:
    explicit BudgetBar(QWidget* parent = nullptr);

    void setBudget(qint64 used, qint64 capacity);
    qint64 used() const { return used_; }
    qint64 capacity() const { return capacity_; }
    bool overBudget() const { return capacity_ > 0 && used_ > capacity_; }

    void setBarThickness(int pixels);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qint64 used_ = 0;
    qint64 capacity_ = 0;
    int thickness_ = 4;
};

}  // namespace rl::ui
