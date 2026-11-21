#pragma once
#include "accessorycontract.h"
#include "controllerruntime.h"
#include "grbl.h"
#include <QElapsedTimer>
#include <QTimer>

// Internal implementation header. No GUI caller may access these objects directly.
class ControllerRuntimeWorker final : public QObject {
    Q_OBJECT
  public:
    ControllerRuntimeWorker(DeviceProfile profile,
                            std::shared_ptr<ControllerRuntimeMailbox> mailbox);
    ~ControllerRuntimeWorker() override;
  private:
    friend class ControllerRuntimeSession;
    friend class ControllerRuntimeTestAccess;
    const DeviceProfile m_profile;
    std::shared_ptr<ControllerRuntimeMailbox> m_mailbox;
    std::unique_ptr<GrblSession> m_core;
    AccessoryContract m_accessory{true};
    QElapsedTimer m_accessoryClock;
    QTimer *m_watchdog = nullptr; // Created in initialize(), after worker affinity is established.
    bool m_jobActive = false, m_focusActive = false;
    bool m_wasVerified = false, m_wasReferenced = false;
    quint64 m_lastFocusInteraction = 0;
    PreparedControllerJob::Ptr m_activePrepared;
    quint64 m_connectionEpoch = 0, m_sequence = 0;
    void initialize();
  private slots:
    void drain();
  private:
    void watchdog();
    void publish();
    void coreChanged();
    bool cancelDispatch();
    void retirePrepared();
    void shutdown();
  signals:
    void latestAvailable();
    void openFinished(quint64 requestId, bool opened, const QString &reason);
    void shutdownFinished();
};
