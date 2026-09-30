#pragma once

#include "theme/tokens.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <memory>

class QApplication;

namespace rl::theme {

// Owns the look of the whole application: style, palette, fonts and the
// generated style sheet. Switching modes re-applies everything at runtime.
class Theme final : public QObject {
    Q_OBJECT

public:
    static Theme& instance();

    // Installs the Industry style on `app`. Call once, before any window.
    void install(QApplication& app, Mode mode);

    Mode mode() const { return mode_; }
    const Tokens& tokens() const { return theme::tokens(mode_); }

    void setMode(Mode mode);
    void toggle();

    // Absolute path of a generated image the style sheet refers to.
    QString assetPath(const QString& name) const;

signals:
    void changed(rl::theme::Mode mode);

private:
    Theme() = default;
    void apply();
    void writeAssets();

    QApplication* app_ = nullptr;
    Mode mode_ = Mode::Dark;
    std::unique_ptr<QTemporaryDir> assets_;
};

// Tokens of the active theme.
const Tokens& current();

}  // namespace rl::theme
