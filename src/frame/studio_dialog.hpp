#pragma once

#include "frame/native_frame.hpp"

#include <QDialog>
#include <QVector>

class QHBoxLayout;
class QVBoxLayout;

namespace rl::frame {

class TitleBar;

// A dialog with the studio title bar in place of the system caption: the
// same frame as StudioWindow, with only a Close button. Content goes into
// body().
class StudioDialog : public QDialog {
    Q_OBJECT

public:
    explicit StudioDialog(const QString& title, QWidget* parent = nullptr);

    TitleBar* titleBar() const { return titleBar_; }
    QWidget* body() const { return body_; }

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
    void showEvent(QShowEvent* event) override;

private:
    TitleBar* titleBar_;
    QWidget* body_;
    NativeFrame frame_;
    bool customFrame_ = false;
};

// A short message with a heading and a row of answers, in the studio look.
// exec() returns the result of the chosen button; Escape and Close return
// the cancel result.
class MessageDialog final : public StudioDialog {
    Q_OBJECT

public:
    MessageDialog(const QString& title, const QString& heading, const QString& text,
                  QWidget* parent = nullptr);

    // Buttons line up on the right in the order added. The primary one takes
    // the accent and Enter.
    void addButton(const QString& text, int result, bool primary = false);
    void setCancelResult(int result) { cancelResult_ = result; }

    void reject() override;

    static void inform(QWidget* parent, const QString& title, const QString& heading,
                       const QString& text);
    // True when the user takes `action`.
    static bool confirm(QWidget* parent, const QString& title, const QString& heading,
                        const QString& text, const QString& action);

private:
    QHBoxLayout* buttons_;
    int cancelResult_ = 0;
};

}  // namespace rl::frame
