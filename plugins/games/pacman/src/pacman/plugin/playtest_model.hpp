#pragma once

#include "sdk/editor_model.hpp"

namespace rl::pacman {

class PacmanSession;

// The running build: Play from selected starts a one-player game at the
// start round, past the title.
class PacmanPlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit PacmanPlaytestModel(PacmanSession* session);

    QString title() const override { return QStringLiteral("Build and run"); }
    sdk::Inspection inspect() const override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;
    QSize screenSize() const override { return {256, 240}; }
    QString coreLabel() const override { return QStringLiteral("MAME · nes"); }
    QString startLabel() const override;
    void play() override;

private:
    PacmanSession* session_;
};

}  // namespace rl::pacman
