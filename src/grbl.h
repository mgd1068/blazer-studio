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
                awaitingReset || handshaking};
    }
    QStringList logEntries() const override { return log; }
    QVariantMap diagnosticSnapshot() const override;
    const DeviceProfile &deviceProfile() const override { return m_deviceProfile; }
    void close() override;
    bool connected() const { return serial.isOpen(); }
    bool ready() const;
    bool verified() const {
        return statusFresh() && connectionVerified && connected() && !handshaking &&
               !awaitingReset && configurationValid() && knownPosition && knownOffset &&
               knownOutput && !activeInputs && (!overrideFeedback || powerOverride <= 100);
    }
    bool configurationValid() const {
        return version && laserMode && powerScale && minimumPower && reportMm && coordinateSystem &&
               bedWidth && bedHeight;
    }
    bool busy() const { return awaitingReset || !queue.isEmpty() || waitingIdle || waitingAck; }
    bool executing() const { return busy() && !handshaking; }
    bool paused = false, firing = false;
    double progress = 0;
    QPointF position;
    QString state = "Getrennt";
    QStringList log;
    void start(const JobPlan &job) override;
    bool overrides(int feed, int power) override;
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
    friend class GrblTest; // Offline PTY fixtures may supply a synthetic machine contract.
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
    std::optional<QRectF> commissionedEnvelope;
    bool commissionedModalContract = false;
    // Separate evidence for switches, directions and homing parameters. Homing
    // itself is physical motion and cannot be authorized by $22 or Idle alone.
    bool commissionedHomingContract = false;
    QElapsedTimer lastStatus;
    void receive();
    void line(const QByteArray &line);
    void pump();
    void
    fail(const QString &message,
         ControllerDiagnostics::FaultCause cause = ControllerDiagnostics::FaultCause::Unspecified);
    void append(const QString &message);
    void handshake();
    bool insideMachineBed(const QPointF &workPoint) const;
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
    bool zeroPending = false;
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
