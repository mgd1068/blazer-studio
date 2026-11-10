#pragma once
#include "job.h"
#include <QObject>
#include <QPointF>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
class GrblSession : public QObject {
    Q_OBJECT
  public:
    explicit GrblSession(QObject *parent = nullptr);
    bool open(const QString &port);
    void close();
    bool connected() const { return serial.isOpen(); }
    bool ready() const;
    bool configurationValid() const {
        return version && laserMode && powerScale && reportMm && coordinateSystem && bedWidth &&
               bedHeight;
    }
    bool busy() const { return !queue.isEmpty() || waitingIdle || waitingAck; }
    bool executing() const { return busy() && !handshaking; }
    bool paused = false, firing = false;
    double progress = 0;
    QPointF position;
    QString state = "Getrennt";
    QStringList log;
    void start(const JobPlan &job);
    bool commands(const QStringList &lines);
    void pause();
    void stop();
    void jog(double dx, double dy);
    void home();
    void zero();
    void focus(bool on);
    void frame(const QVariantMap &bounds);
  signals:
    void changed();

  private:
    void receive();
    void line(const QByteArray &line);
    void pump();
    void fail(const QString &message);
    void append(const QString &message);
    void handshake();
    QSerialPort serial;
    QTimer poll, timeout, focusTimer;
    QByteArray input;
    QStringList queue;
    bool waitingAck = false, waitingIdle = false, handshaking = false;
    bool reportMm = false, coordinateSystem = false, bedWidth = false, bedHeight = false;
    bool version = false, laserMode = false, powerScale = false, knownPosition = false,
         homing = false;
    QString controllerState;
    QPointF offset;
    bool knownOffset = false;
    int total = 0, acked = 0, pendingAcks = 0;
};
