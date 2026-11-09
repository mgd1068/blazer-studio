#include "simulator.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>
Simulator::Simulator(QObject *p) : QObject(p) {
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &Simulator::tick);
    m_focusLimit.setSingleShot(true);
    m_focusLimit.setInterval(3000);
    connect(&m_focusLimit, &QTimer::timeout, this, [this] { focus(false); });
    append("Simulator bereit · keine Hardwareverbindung");
}
void Simulator::append(const QString &s) {
    m_log.prepend(QTime::currentTime().toString("HH:mm:ss") + "  " + s);
    while (m_log.size() > 30)
        m_log.removeLast();
    emit changed();
}
void Simulator::toggleConnection() {
    if (m_connected) {
        stop();
        m_connected = false;
        m_state = "Getrennt";
        append("Simulator getrennt");
    } else {
        m_connected = true;
        m_state = "Bereit";
        m_firing = false;
        append("Simulator verbunden · Laser bleibt aus");
    }
}
void Simulator::disconnectFault() {
    stop();
    m_connected = false;
    m_state = "Verbindungsfehler";
    append("Verbindungsabbruch simuliert · Freigabe aufgehoben");
}
void Simulator::jog(double dx, double dy) {
    if (!m_connected || busy() || m_firing || !std::isfinite(dx) || !std::isfinite(dy))
        return;
    m_x = std::clamp(m_x + dx, 0., 200.);
    m_y = std::clamp(m_y + dy, 0., 150.);
    append(QString("Position %1 / %2 mm").arg(m_x, 0, 'f', 1).arg(m_y, 0, 'f', 1));
}
void Simulator::home() {
    if (!m_connected || busy() || m_firing)
        return;
    m_x = m_y = 0;
    append("Referenzfahrt simuliert");
}
void Simulator::focus(bool on) {
    if (m_firing == on)
        return;
    if (on && (!m_connected || busy()))
        return;
    m_firing = on;
    if (on)
        m_focusLimit.start();
    else
        m_focusLimit.stop();
    append(on ? "2 % Fokuspunkt simuliert · max. 3 Sekunden" : "Laser-Aus simuliert");
}
void Simulator::frame() {
    if (!m_connected || busy() || m_firing)
        return;
    m_state = "Rahmenfahrt";
    m_progress = 0;
    m_tick.start();
    append("Rahmenfahrt simuliert · ohne Emission");
}
void Simulator::start() {
    if (!m_connected || busy() || m_firing)
        return;
    m_state = "Läuft";
    m_progress = 0;
    m_tick.start();
    append("Demoablauf gestartet · kein berechneter Maschinenjob");
}
void Simulator::pause() {
    if (m_state == "Läuft") {
        m_state = "Pausiert";
        m_tick.stop();
        append("Demo pausiert");
    } else if (m_state == "Pausiert") {
        m_state = "Läuft";
        m_tick.start();
        append("Demo fortgesetzt");
    }
}
void Simulator::stop() {
    m_tick.stop();
    m_focusLimit.stop();
    m_firing = false;
    m_state = m_connected ? "Bereit" : "Getrennt";
    m_progress = 0;
    append("Stop · alle simulierten Ausgaben aus");
}
void Simulator::tick() {
    m_progress = std::min(1., m_progress + (m_state == "Rahmenfahrt" ? .025 : .006));
    m_x = 30 + 140 * m_progress;
    m_y = 35 + 80 * (.5 + .5 * std::sin(m_progress * 6.283));
    if (m_progress >= 1) {
        m_tick.stop();
        m_state = "Bereit";
        append("Simulation abgeschlossen");
    }
    emit changed();
}
