#pragma once
#include "controllersession.h"
#include "preparedcontrollerjob.h"
#include "job.h"
#include <QMutex>
#include <QThread>
#include <atomic>
#include <memory>

class ControllerRuntimeWorker;
class ControllerRuntimeTestAccess;
class ControllerRuntimeHost;
struct ControllerRuntimeMailbox;
class ControllerNativePressProof final {
    friend class Machine;
    friend class ControllerRuntimeSession;
    friend class ControllerRuntimeWorker;
    friend class ControllerRuntimeTestAccess;
    ControllerNativePressProof(quint64 issuerValue, quint64 interactionValue, quint64 windowValue,
                               qint64 observed, qint64 expires)
        : issuer(issuerValue), interaction(interactionValue), window(windowValue),
          observedAt(observed), expiresAt(expires) {}
    const quint64 issuer, interaction, window;
    const qint64 observedAt, expiresAt;
};
struct ControllerRuntimeInputStamp {
    quint64 interactionId = 0, windowEpoch = 0;
    qint64 observedAtMs = -1, expiresAtMs = -1;
    std::shared_ptr<const ControllerNativePressProof> proof;
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
    void prepare(const JobPlan &plan);
    bool jobPreparing() const;
    bool jobPrepared() const;
    QString prepareReason() const;
    ControllerSubmission requestStart(PreparedControllerJob::Ptr job);
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
    friend class Machine;
    friend class ControllerRuntimeWorker;
    void cancelPendingOpen();
    const DeviceProfile m_profile;
    std::shared_ptr<ControllerRuntimeHost> m_host;
    ControllerRuntimeWorker *m_worker = nullptr;
    std::shared_ptr<ControllerRuntimeMailbox> m_mailbox;
    ControllerStatus m_cache;
    QStringList m_log;
    QVariantMap m_diagnostic;
    quint64 m_nextRequest = 0, m_pendingOpen = 0;
    JobMoves m_candidateMoves;
    QByteArray m_candidateProfile;
    quint64 m_prepareSequence = 0;
    ControllerRuntimeInputStamp sealNativePress(quint64 interaction, quint64 window,
                                                qint64 observedAt);
    void revokeFocusContext();
    void consumeLatest();
};
