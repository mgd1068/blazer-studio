#pragma once
#include "controllersession.h"
#include "job.h"
#include <QObject>
#include <QPointF>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
class GrblSession : public ControllerSession {
    Q_OBJECT
  public:
    explicit GrblSession(QObject *parent = nullptr,
                         DeviceProfile profile = DeviceProfile::m3Reference());
    bool open(const QString &port) override;
    ControllerStatus status() const override {
        return {connected(), ready(),  busy(),   executing(), paused,
                firing,      progress, position, state};
    }
    QStringList logEntries() const override { return log; }
    const DeviceProfile &deviceProfile() const override { return m_deviceProfile; }
    void close() override;
    bool connected() const { return serial.isOpen(); }
    bool ready() const;
    bool configurationValid() const {
        return version && laserMode && powerScale && minimumPower && reportMm && coordinateSystem &&
               bedWidth && bedHeight;
    }
    bool busy() const { return !queue.isEmpty() || waitingIdle || waitingAck; }
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
    bool commands(const QStringList &lines, bool buffered = false);
    void pause() override;
    void stop() override;
    void jog(double dx, double dy) override;
    void home() override;
    void zero() override;
    void focus(bool on) override;
    void frame(const QVariantMap &bounds) override;

  private:
    void receive();
    void line(const QByteArray &line);
    void pump();
    void fail(const QString &message);
    void append(const QString &message);
    void handshake();
    bool insideMachineBed(const QPointF &workPoint) const;
    const DeviceProfile m_deviceProfile;
    QSerialPort serial;
    QTimer poll, timeout, focusTimer;
    QByteArray input;
    QStringList queue;
    bool waitingAck = false, waitingIdle = false, handshaking = false;
    bool reportMm = false, coordinateSystem = false, bedWidth = false, bedHeight = false;
    bool version = false, laserMode = false, powerScale = false, knownPosition = false,
         homing = false;
    bool minimumPower = false, knownOutput = false, activeInputs = false;
    double reportedOutput = 0;
    bool contaminatedConfiguration = false, pauseRequested = false;
    bool handlingFailure = false;
    bool zeroPending = false;
    int configurationRetries = 0;
    QString controllerState;
    QPointF offset;
    bool knownOffset = false;
    QList<int> inFlight;
    int sent = 0, rxBytes = 0;
    bool buffering = false;
    int total = 0, acked = 0, pendingAcks = 0;
};
