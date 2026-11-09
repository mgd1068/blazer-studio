#pragma once
#include <QObject>
#include <QTimer>
class Simulator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool firing READ firing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QStringList log READ log NOTIFY changed)
  public:
    explicit Simulator(QObject *parent = nullptr);
    bool connected() const { return m_connected; }
    QString state() const { return m_state; }
    double x() const { return m_x; }
    double y() const { return m_y; }
    double progress() const { return m_progress; }
    bool firing() const { return m_firing; }
    bool busy() const {
        return m_state == "Läuft" || m_state == "Pausiert" || m_state == "Rahmenfahrt";
    }
    QStringList log() const { return m_log; }
    Q_INVOKABLE void toggleConnection();
    Q_INVOKABLE void jog(double dx, double dy);
    Q_INVOKABLE void home();
    Q_INVOKABLE void focus(bool on);
    Q_INVOKABLE void frame();
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void disconnectFault();
  signals:
    void changed();

  private:
    void append(const QString &s);
    void tick();
    bool m_connected = true, m_firing = false;
    QString m_state = "Bereit";
    double m_x = 100, m_y = 75, m_progress = 0;
    QTimer m_tick, m_focusLimit;
    QStringList m_log;
};
