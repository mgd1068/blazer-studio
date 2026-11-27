#pragma once
#include "controllerdiagnostics.h"
#include "controllersession.h"
#include "job.h"
#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
#include <optional>
#include <QMap>
#include <QSet>
#include <functional>
class PreparedControllerJob;
class GrblSession : public ControllerSession {
    Q_OBJECT
  public:
    explicit GrblSession(QObject *parent = nullptr,
                         DeviceProfile profile = DeviceProfile::m3Reference());
    ~GrblSession() override;
    bool open(const QString &port) override;
    ControllerStatus status() const override {
        return {connected(),
                ready(),
                busy(),
                executing(),
                paused,
                firing,
                progress,
                position,
                state,
                verified(),
                positionReferenced,
                motionSafetyError(),
                homingSafetyError(),
                homingPending,
                awaitingReset,
                overridesReady(),
                overridesSafetyError(),
                workZero.has_value()};
    }
    QStringList logEntries() const override { return log; }
    QVariantMap diagnosticSnapshot() const override;
    const DeviceProfile &deviceProfile() const override { return m_deviceProfile; }
    void close() override;
    bool connected() const { return serial.isOpen(); }
    bool ready() const;
    bool verified() const {
        return statusFresh() && connectionVerified && connected() && !handshaking &&
               !awaitingReset && !workZero && configurationValid() && knownPosition &&
               knownOffset && knownOutput && !activeInputs &&
               (!overrideFeedback || powerOverride <= 100);
    }
    bool configurationValid() const {
        return version && laserMode && powerScale && minimumPower && reportMm && coordinateSystem &&
               bedWidth && bedHeight;
    }
    bool busy() const {
        return awaitingReset || workZero || !queue.isEmpty() || waitingIdle || waitingAck;
    }
    bool executing() const { return busy() && !handshaking; }
    bool paused = false, firing = false;
    double progress = 0;
    QPointF position;
    QString state = "Getrennt";
    QStringList log;
    void start(const JobPlan &job) override;
    bool overrides(int feed, int power) override;
    bool overridesReady() const;
    QString overridesSafetyError() const;
    int feedOverride = 100, powerOverride = 100;
    bool overrideFeedback = false;
    QString motionSafetyError() const;
    QString homingSafetyError() const;
    void pause() override;
    void stop() override;
    void jog(double dx, double dy) override;
    void home() override;
    void zero() override;
    void focus(bool on) override;
    void frame(const QVariantMap &bounds) override;

  private:
    friend class GrblBackpressureTest;
    friend class GrblTest; // Offline PTY fixtures may supply a synthetic machine contract.
    friend class ControllerRuntimeWorker;
    friend class ControllerRuntimeTestAccess;
    std::function<bool()> dispatchCancelled;
    std::function<bool()> dispatchPreflight;
    std::function<bool()> homingDispatchAllowed; // Exact $H write boundary, if supplied.
    std::function<void()> homingDispatchAccepted;
    std::function<void()> dispatchAccepted; // Host write acceptance, never physical movement proof.
    QTimer ackTimeout;
    QElapsedTimer ackClock;
    QList<qint64> ackDue;
    qint64 ackPausedAt = -1;
    void clearAckDeadline();
    void armAckDeadline();
    void acknowledgeDeadline();
    void pauseAckDeadline();
    void resumeAckDeadline();
    struct BufferedJobDrain {
        enum class Phase {Streaming,BeforeM5,Finalizing,AfterM5};
        Phase phase=Phase::Streaming;
        qsizetype m5Index=0;
        bool queryArmed=false,positionKnown=false;
        QPointF lastMachinePosition;
        qint64 hardBudgetMs=30000,activeMs=0,noProgressMs=0,lastTick=0;
    };
    std::optional<BufferedJobDrain> bufferedJobDrain;
    QElapsedTimer bufferedDrainClock;
    QTimer bufferedDrainWatchdog;
    quint64 bufferedDrainEpoch=0;
    bool startBufferedJob(const QStringList &lines,double estimatedSeconds);
    void clearBufferedJobDrain();
    void beginBufferedJobDrain();
    void armBufferedJobReadback(quint64 epoch);
    void checkBufferedJobDrain();
    bool observeBufferedJobStatus();
    int queueHead = 0;
    struct OperatorLocalPermit {
        QByteArray frameId, settingsFingerprint;
        QPointF counterAtCapture, capturedHead, wco;
        QRectF usableDisplacement;
    };
    std::optional<OperatorLocalPermit> operatorLocalPermit;
    struct LocalCaptureResult { QPointF counter, head, wco; };
    std::optional<LocalCaptureResult> localCaptureResult;
    enum class LocalExecutionKind {None,Job,FrozenS0Frame};
    LocalExecutionKind localExecutionKind=LocalExecutionKind::None;
    bool localExecutionActive=false;
    quint64 startupSequence=0;
    int doorSubstate=-1;
    QString localMotionSafetyError() const;
    bool localTargetAllowed(QPointF work) const;
    bool beginLocalCoordinateCapture(QPointF workHead);
    bool startLocalPrepared(const PreparedControllerJob &job);
    bool frameLocalPrepared(const QStringList &lines);
    bool startPrepared(const PreparedControllerJob &job, std::optional<QPointF> machineHome = {});
    struct JobReturn {
        enum class Phase { Engraving, Returning, Parked } phase=Phase::Engraving;
        QPointF home, wco, from;
        QRectF allowed;
        bool queryArmed=false, initialApproach=false;
    };
    std::optional<JobReturn> jobReturn;
    quint64 jobReturnEpoch=0;
    void clearJobReturn();
    void armJobReturnReadback(quint64 epoch);
    bool beginJobReturn();
    bool jobReturnPositionAllowed() const;
    bool commands(const QStringList &lines, bool buffered = false);
    bool statusFresh() const;
    ControllerDiagnostics::LiveState diagnosticState() const;
    ControllerDiagnostics::Denial diagnosticMotionDenial() const;
    void auditDispatch(const QByteArray &block);
    ControllerDiagnostics diagnostic;
    bool diagnosticNormalizedAck = false;
    int diagnosticSpindle = 0;
    double diagnosticPower = 0;
    // Deliberately absent on real sessions: neither a profile nor a homing ACK
    // proves signed travel limits or the active WCO/G54 modal contract. A future
    // commissioning workflow must bind these to a verified controller identity.
    struct SetupReadback {
        QMap<int,double> settings;
        QSet<int> startup;
        bool pending=false, complete=false, valid=false, version=false, modal=false, operatorLocal=false;
        bool g54Known=false, g92Known=false, tloKnown=false;
        QPointF g54, g92;
        double tlo=0;
        QByteArray fingerprint, firmware, deviceIdentityHash;
        QString error;
    } setupReadback;
    bool setupReadyForAttestation() const;
    bool beginSetupReadback(bool operatorLocal=false);
    void captureSetupReadback(const QByteArray &line);
    void finishSetupReadback();
    std::optional<QRectF> commissionedEnvelope;
    bool commissionedModalContract = false;
    // Independent evidence of ordered, exactly-once realtime byte processing
    // and 10..200% clamping; standard GRBL flag coalescing does not prove this.
    // No production grant exists; a banner or Ov readback cannot prove this.
    bool commissionedOverrideContract = false;
    // Separate evidence for switches, directions and homing parameters. Homing
    // itself is physical motion and cannot be authorized by $22 or Idle alone.
    bool commissionedHomingContract = false;
    QElapsedTimer lastStatus;
    QElapsedTimer lastOverrideFeedback;
    bool overrideReadbackPending = false;
    int requestedFeedOverride = 100, requestedPowerOverride = 100;
    struct WorkZeroTransaction {
        QPointF machine, previousOffset;
        enum class Phase { Acknowledgements, Readback } phase = Phase::Acknowledgements;
        bool g10Dispatched = false, modalRequested = false, modalReadback = false;
        bool queryArmed = false;
        bool operatorLocal = false;
        QPointF targetWork{};

    };
    std::optional<WorkZeroTransaction> workZero;
    quint64 workZeroEpoch = 0;
    QTimer workZeroDeadline;
    void clearWorkZero();
    void armWorkZeroReadback(quint64 epoch);
    bool checkWorkZeroStatus(bool hasMachine, bool offsetReported);
    void receive();
    void line(const QByteArray &line);
    void pump();
    void
    fail(const QString &message,
         ControllerDiagnostics::FaultCause cause = ControllerDiagnostics::FaultCause::Unspecified);
    void append(const QString &message);
    void handshake();
    bool insideMachineBed(const QPointF &workPoint) const;
    bool insideRawMachineBed(const QPointF &machinePoint) const;
    bool insideReportedMachineBed() const;
    bool rejectUnsafeReportedMachinePosition();
    QPointF reportedMachinePosition;
    bool knownReportedMachinePosition = false;
    const DeviceProfile m_deviceProfile;
    QSerialPort serial;
    QTimer poll, timeout, focusTimer;
    QByteArray input;
    QStringList queue;
    bool waitingAck = false, waitingIdle = false, handshaking = false;
    bool awaitingReset = false, connectionVerified = false;
    bool reportMm = false, coordinateSystem = false, bedWidth = false, bedHeight = false;
    bool version = false, laserMode = false, powerScale = false, knownPosition = false,
         homing = false;
    bool minimumPower = false, knownOutput = false, activeInputs = false;
    double reportedOutput = 0;
    bool contaminatedConfiguration = false, pauseRequested = false;
    bool handlingFailure = false;
    bool positionReferenced = false, homingPending = false;
    int configurationRetries = 0;
    QString controllerState;
    QPointF offset;
    bool knownOffset = false;
    QList<int> inFlight;
    int sent = 0, rxBytes = 0;
    bool buffering = false;
    int total = 0, acked = 0, pendingAcks = 0;
};
