#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantMap>
class Simulator : public QObject {
    Q_OBJECT
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
    bool connected() const { return m_mode != Mode::Disconnected && m_mode != Mode::Fault; }
    QString state() const;
    double x() const { return m_x; }
    double y() const { return m_y; }
    double progress() const { return m_progress; }
    bool firing() const { return m_firing; }
    bool busy() const {
        return m_mode == Mode::Running || m_mode == Mode::Paused || m_mode == Mode::Framing;
    }
    bool paused() const { return m_mode == Mode::Paused; }
    bool running() const { return m_mode == Mode::Running; }
    QStringList log() const { return m_log; }
    Q_INVOKABLE void toggleConnection();
    Q_INVOKABLE void jog(double dx, double dy);
    Q_INVOKABLE void home();
    Q_INVOKABLE void focus(bool on);
    Q_INVOKABLE void frame(const QVariantMap &bounds);
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void disconnectFault();
  signals:
    void changed();

  private:
    void append(const QString &s);
    void clearOutputs();
    void tick();
    Mode m_mode = Mode::Ready;
    bool m_firing = false;
    double m_x = 100, m_y = 75, m_progress = 0;
    qint64 m_elapsedMs = 0;
    QRectF m_frame;
    QElapsedTimer m_clock;
    QTimer m_tick, m_focusLimit;
    QStringList m_log;
};
