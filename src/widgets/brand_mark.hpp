#pragma once

#include <QWidget>

namespace rl::ui {

// The Retrolarium mark at a fixed size, in the variant of the current theme.
class BrandMark final : public QWidget {
public:
    explicit BrandMark(int size, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int size_;
};

}  // namespace rl::ui
