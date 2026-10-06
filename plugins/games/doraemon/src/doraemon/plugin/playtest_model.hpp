#pragma once

#include "sdk/editor_model.hpp"

namespace rl::doraemon {

class DoraemonSession;

// The running build, from power-on or from a world past the title.
class DoraemonPlaytestModel final : public sdk::EmulatorModel {
    Q_OBJECT

public:
    explicit DoraemonPlaytestModel(DoraemonSession* session);

    QString title() const override { return tr("Build and run"); }
    sdk::Inspection inspect() const override;
    QSize screenSize() const override { return {256, 240}; }
    QString coreLabel() const override { return QStringLiteral("MAME · nes"); }
    QString startLabel() const override;
    void play() override;
    bool setField(const QString& key, const QVariant& value, QString* error) override;

private:
    DoraemonSession* session_;
};

}  // namespace rl::doraemon
