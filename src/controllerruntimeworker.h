#pragma once
#include "accessorycontract.h"
#include "controllercommissioning.h"
#include "controllerruntime.h"
#include "grbl.h"
#include "operatorlocalframe.h"
#include "preparedlocaljob.h"
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
    OperatorLocalFrameState m_local;
    PreparedLocalJob::Ptr m_localCandidate,m_localActive;
    bool m_localFrameRun=false,m_updatingLocal=false;
    qint64 m_localFirstMotionDue=-1;
    QPointF m_localMotionStartCounter;
    void localData(const ControllerRuntimeRequest &);
    void localMotion(const ControllerRuntimeRequest &);
    void updateLocalFrame();
    void invalidateLocalFrame(const QString &reason);
    QString runtimeMotionError() const;
    QVariantMap localFrameSnapshot() const;
    QByteArray supervisionBinding() const;
    ControllerCommissioning m_commissioning;
    bool m_setupUpdating=false, m_calibrationReadbackPending=false;
    QByteArray m_calibrationBinding;
    QPointF m_attestedWorkOffset;
    bool m_attestedWorkOffsetKnown=false;
    QVariantMap m_calibrationCandidate;
    QString m_restoreReason;
    std::shared_ptr<const ConnectionHomePermit> m_connectionHomePermit;
    quint64 m_connectionHomeEpoch=0,m_connectionHomeRequestId=0;
    QString m_connectionHomeEndpoint;
    bool m_connectionHomeStarted=false;
    bool validateConnectionHomePermitAtWrite();
    bool acceptConnectionHomePermit(const ControllerRuntimeRequest &request);
    void discardConnectionHomePermit();
    void startCalibratedHome();
    void beginCalibrationRestore(const QVariantMap &record,const QByteArray &binding);
    void clearCalibrationSession();
    bool matchCalibrationCandidate();
    void finishCalibrationReference();
    void publishCalibrationRecord();
    QByteArray m_operatorSession, m_operatorSource, m_activeOperatorOperation;
    quint64 m_operatorAttempt=0, m_operatorWitnessCoordinate=0;
    bool m_operatorPaused=false;
    QString m_operatorAction, m_operatorReason;
    std::optional<AccessoryContract::OperatorWitness> m_operatorWitness;
    AccessoryContract::OperatorContext m_operatorContext;
    void operatorData(const QVariantMap &data);
    bool beginAccessory(AccessoryContract::OperatorAction action,const QByteArray &source = {});
    bool singleClickStartEligible() const;
    QString attendedStartError() const;
    QString referenceHomeError() const;
    QString calibratedHomeError(bool allowApproach) const;
    struct AttendedStartOperation {
        PreparedControllerJob::Ptr prepared;
        quint64 connection=0,generation=0,sourceGeneration=0,window=0;
        qint64 observedAt=0,admittedAt=0,deadline=0,relativeObservedAt=0,relativeAdmittedAt=0;
        QByteArray binding,profile,settings;
        QPointF offset,home;
        QRectF limits;
        QString phase="homing";
        bool homeWritten=false,referenceOnly=false;
    };
    std::optional<AttendedStartOperation> m_attendedStart;
    bool m_updatingAttendedStart=false,m_attendedStartUpdateQueued=false;
    QString m_attendedStartReason,m_attendedStartFailure;
    bool beginAttendedStart(const ControllerRuntimeRequest &request);
    bool beginReferenceHome(const ControllerRuntimeRequest &request);
    bool commitCalibratedReference(const ControllerRuntimeRequest &request,bool referenceOnly);
    QString attendedStartInvalidationReason(bool atHomeWrite) const;
    bool validateAttendedStart(bool atHomeWrite=false);
    void updateAttendedStart();
    void scheduleAttendedStart();
    void discardAttendedStart(const QString &reason={});
    bool noOutputIdle() const;
    // True only before first Open or for an explicitly observed safe idle Close.
    bool m_noOutputLifecycle=true;
    void discardOperatorWitness();
    QVariantMap extractionSupervisionSnapshot() const;
    void setupData(const QVariantMap &data);
    void setupMotion(const ControllerRuntimeRequest &request);
    void updateCommissioning();
    void cancelCommissioning(const QString &reason);
    QVariantMap commissioningSnapshot() const;
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
    void calibrationCompleted(QByteArray binding,QVariantMap record);
    void openFinished(quint64 requestId, bool opened, const QString &reason);
    void shutdownFinished();
};
