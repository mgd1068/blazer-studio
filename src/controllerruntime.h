#pragma once
#include "controllersession.h"
#include <QMutex>
#include <QThread>
#include <atomic>
#include <memory>

class ControllerRuntimeWorker;
class ControllerRuntimeTestAccess;
class ControllerRuntimeHost;
struct ControllerRuntimeMailbox;
struct ControllerRuntimeInputStamp {
    quint64 interactionId = 0, windowEpoch = 0;
    qint64 observedAtMs = -1, expiresAtMs = -1;
};

// Production facade only. All I/O, motion authority and leases belong to the worker.
// There is deliberately no synchronous-runtime configuration switch.
class ControllerRuntimeSession final : public ControllerSession {
    Q_OBJECT
  public:
    explicit ControllerRuntimeSession(DeviceProfile profile, QObject *parent = nullptr);
    ~ControllerRuntimeSession() override;
    const DeviceProfile &deviceProfile() const override { return m_profile; }
    ControllerStatus status() const override;
    QStringList logEntries() const override;
    QVariantMap diagnosticSnapshot() const override;
    ControllerSubmission requestOpen(const QString &endpoint,
                                     const QByteArray &bindingDigest) override;
    // Legacy C++ admission wrapper; never blocks for open/handshake.
    bool open(const QString &endpoint) override;
    void close() override;
    void start(const JobPlan &plan) override;
    void pause() override;
    void stop() override;
    void jog(double dx, double dy) override;
    void home() override;
    void zero() override;
    void focus(bool on) override;
    void frame(const QVariantMap &bounds) override;
    bool overrides(int feed, int power) override;
    void beginFocus(const ControllerRuntimeInputStamp &stamp);
    void endFocus();

  private:
    friend class ControllerRuntimeTestAccess;
    const DeviceProfile m_profile;
    std::shared_ptr<ControllerRuntimeHost> m_host;
    ControllerRuntimeWorker *m_worker = nullptr;
    std::shared_ptr<ControllerRuntimeMailbox> m_mailbox;
    ControllerStatus m_cache;
    QStringList m_log;
    QVariantMap m_diagnostic;
    quint64 m_nextRequest = 0, m_pendingOpen = 0;
    void consumeLatest();
};
