#pragma once
#include "job.h"
#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantMap>
class Simulator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool firing READ firing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QStringList log READ log NOTIFY changed)
  public:
    enum class Mode { Ready, Disconnected, Running, Paused, Framing, Fault };
    explicit Simulator(QObject *parent = nullptr);
    void setJob(const JobPlan &job) {
        if (!busy())
            m_job = job;
    }
    virtual bool ready() const { return connected() && !busy() && !firing(); }
    virtual bool connected() const { return m_mode != Mode::Disconnected && m_mode != Mode::Fault; }
    virtual QString state() const;
    virtual double x() const { return m_x; }
    virtual double y() const { return m_y; }
    virtual double progress() const { return m_progress; }
    virtual bool firing() const { return m_firing; }
    virtual bool busy() const {
        return m_mode == Mode::Running || m_mode == Mode::Paused || m_mode == Mode::Framing;
    }
    virtual bool paused() const { return m_mode == Mode::Paused; }
    virtual bool running() const { return m_mode == Mode::Running; }
    virtual QStringList log() const { return m_log; }
    Q_INVOKABLE virtual void toggleConnection();
    Q_INVOKABLE virtual void jog(double dx, double dy);
    Q_INVOKABLE virtual void home();
    Q_INVOKABLE virtual void focus(bool on);
    Q_INVOKABLE virtual void frame(const QVariantMap &bounds);
    Q_INVOKABLE virtual void start();
    Q_INVOKABLE virtual void pause();
    Q_INVOKABLE virtual void stop();
    Q_INVOKABLE virtual void disconnectFault();
  signals:
    void changed();

  private:
    void append(const QString &s);
    void clearOutputs();
    void tick();
    JobPlan m_job;
    Mode m_mode = Mode::Ready;
    bool m_firing = false;
    double m_x = 100, m_y = 75, m_progress = 0;
    qint64 m_elapsedMs = 0;
    QRectF m_frame;
    QElapsedTimer m_clock;
    QTimer m_tick, m_focusLimit;
    QStringList m_log;
};
