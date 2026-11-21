#pragma once
#include "accessorycontract.h"
#include "controllerruntime.h"
#include "grbl.h"
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <optional>
struct ControllerRuntimeRequest;

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
    quint64 m_coordinateEpoch = 1, m_lastNativeInteraction = 0;
    std::shared_ptr<const ControllerNativePressProof> m_dispatchProof;
    bool m_epochKnownOffset=false,m_epochReferenced=false,m_epochHoming=false,m_epochResetting=false;
    QPointF m_epochOffset;
    void refreshCoordinateEpoch();
    bool nativeActionAllowed(const ControllerRuntimeRequest &request);
    struct ClockInterval { qint64 lowNs, highNs; };
    static std::optional<ClockInterval> readClockInterval();
    // Private synthetic seam; no configuration/UI can override the real clock.
    std::function<std::optional<ClockInterval>()> m_clockSample = readClockInterval;
    std::optional<ClockInterval> m_clockBaseline;
    bool m_continuityHandling = false;
    std::function<void()> m_stopHandoffProbe; // Private test boundary; unset in production.
    bool checkClockContinuity();
    void initialize();
  private slots:
    void drain();

  private:
    void watchdog();
    void publish(bool finishDispatch = false);
    void coreChanged();
    bool cancelDispatch();
    void retirePrepared();
    void shutdown();
  signals:
    void latestAvailable();
    void openFinished(quint64 requestId, bool opened, const QString &reason);
    void shutdownFinished();
};
