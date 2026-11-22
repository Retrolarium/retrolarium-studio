#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

template <typename T>
class QFutureWatcher;

namespace rl::runtime {

// One bounded ROM read at a time. Cancellation discards the result without
// waiting for filesystem I/O; the worker never captures its QObject owner.
class RomHasher final : public QObject {
    Q_OBJECT
public:
    struct Result {
        QString digest;
        QString error;
        bool tooLarge = false;
    };
    using Cancelled = std::shared_ptr<std::atomic_bool>;
    using Hash = std::function<Result(const QString&, qint64, const Cancelled&)>;

    explicit RomHasher(QObject* parent = nullptr, Hash hash = {});
    ~RomHasher() override;
    bool start(const QString& path, qint64 maxBytes);
    void cancel();
    bool isBusy() const { return pending_ != nullptr; }

signals:
    void stateChanged();
    void finished(const QString& path, const QString& digest, const QString& error, bool tooLarge);

private:
    static Result calculate(const QString& path, qint64 maxBytes, const Cancelled& cancelled);
    Hash hash_;
    QFutureWatcher<Result>* pending_ = nullptr;
    Cancelled cancelled_;
};

}  // namespace rl::runtime
