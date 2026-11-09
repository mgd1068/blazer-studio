#include "simulator.h"
#include "geometry.h"
#include <QTime>
#include <algorithm>
#include <cmath>
Simulator::Simulator(QObject *p) : QObject(p) {
    m_tick.setInterval(50);
    connect(&m_tick, &QTimer::timeout, this, &Simulator::tick);
    m_focusLimit.setSingleShot(true);
    m_focusLimit.setInterval(3000);
    connect(&m_focusLimit, &QTimer::timeout, this, [this] { focus(false); });
    append("Simulator bereit · keine Hardwareverbindung");
}
QString Simulator::state() const {
    switch (m_mode) {
    case Mode::Ready:
        return "Bereit";
    case Mode::Disconnected:
        return "Getrennt";
    case Mode::Running:
        return "Läuft";
    case Mode::Paused:
        return "Pausiert";
    case Mode::Framing:
        return "Rahmenfahrt";
    case Mode::Fault:
        return "Verbindungsfehler";
    }
    return {};
}
void Simulator::append(const QString &s) {
    m_log.prepend(QTime::currentTime().toString("HH:mm:ss") + "  " + s);
    while (m_log.size() > 30)
        m_log.removeLast();
    emit changed();
}
void Simulator::clearOutputs() {
    m_tick.stop();
    m_focusLimit.stop();
    m_firing = false;
    m_progress = 0;
    m_elapsedMs = 0;
}
void Simulator::toggleConnection() {
    bool wasConnected = connected();
    clearOutputs();
    m_mode = wasConnected ? Mode::Disconnected : Mode::Ready;
    append(wasConnected ? "Simulator getrennt" : "Simulator verbunden · Laser bleibt aus");
}
void Simulator::disconnectFault() {
    clearOutputs();
    m_mode = Mode::Fault;
    append("Verbindungsabbruch simuliert · Freigabe aufgehoben");
}
void Simulator::jog(double dx, double dy) {
    if (m_mode != Mode::Ready || m_firing || !std::isfinite(dx) || !std::isfinite(dy))
        return;
    m_x = std::clamp(m_x + dx, 0., Geometry::bedWidth);
    m_y = std::clamp(m_y + dy, 0., Geometry::bedHeight);
    append(QString("Position %1 / %2 mm").arg(m_x, 0, 'f', 1).arg(m_y, 0, 'f', 1));
}
void Simulator::home() {
    if (m_mode != Mode::Ready || m_firing)
        return;
    m_x = m_y = 0;
    append("Referenzfahrt simuliert");
}
void Simulator::focus(bool on) {
    if (m_firing == on || (on && m_mode != Mode::Ready))
        return;
    m_firing = on;
    if (on)
        m_focusLimit.start();
    else
        m_focusLimit.stop();
    append(on ? "2 % Fokuspunkt simuliert · max. 3 Sekunden" : "Laser-Aus simuliert");
}
void Simulator::frame(const QVariantMap &bounds) {
    if (m_mode != Mode::Ready || m_firing)
        return;
    for (const auto &key : {"x", "y", "width", "height"}) {
        bool ok = false;
        auto n = bounds.value(key).toDouble(&ok);
        if (!ok || !std::isfinite(n)) {
            append("Rahmenfahrt: ungültige Grenzen.");
            return;
        }
    }
    QRectF rect(bounds["x"].toDouble(), bounds["y"].toDouble(), bounds["width"].toDouble(),
                bounds["height"].toDouble());
    if (!bounds["valid"].toBool() || !Geometry::insideBed(rect)) {
        append("Rahmenfahrt abgelehnt: kein aktives Motiv oder außerhalb des Arbeitsraums.");
        return;
    }
    m_frame = rect;
    m_mode = Mode::Framing;
    m_progress = 0;
    m_elapsedMs = 0;
    m_clock.start();
    m_tick.start();
    auto p = Geometry::framePoint(rect, 0);
    m_x = p.x();
    m_y = p.y();
    append("Rahmenfahrt um aktive Objektgrenzen simuliert · ohne Emission");
}
void Simulator::start() {
    if (m_mode != Mode::Ready || m_firing)
        return;
    m_mode = Mode::Running;
    m_progress = 0;
    m_elapsedMs = 0;
    m_clock.start();
    m_tick.start();
    append("Demoablauf gestartet · kein berechneter Maschinenjob");
}
void Simulator::pause() {
    if (m_mode == Mode::Running) {
        m_elapsedMs += m_clock.elapsed();
        m_mode = Mode::Paused;
        m_tick.stop();
        append("Demo pausiert");
    } else if (m_mode == Mode::Paused) {
        m_mode = Mode::Running;
        m_clock.start();
        m_tick.start();
        append("Demo fortgesetzt");
    }
}
void Simulator::stop() {
    bool wasConnected = connected();
    clearOutputs();
    if (wasConnected)
        m_mode = Mode::Ready;
    append("Stop · alle simulierten Ausgaben aus");
}
void Simulator::tick() {
    if (m_mode != Mode::Running && m_mode != Mode::Framing)
        return;
    m_progress = std::clamp(double(m_elapsedMs + m_clock.elapsed()) /
                                (m_mode == Mode::Framing ? 4000. : 16000.),
                            0., 1.);
    if (m_mode == Mode::Framing) {
        auto p = Geometry::framePoint(m_frame, m_progress);
        m_x = p.x();
        m_y = p.y();
    } else {
        m_x = 30 + 140 * m_progress;
        m_y = 35 + 80 * (.5 + .5 * std::sin(m_progress * 6.283185307));
    }
    if (m_progress >= 1) {
        m_tick.stop();
        m_mode = Mode::Ready;
        append("Simulation abgeschlossen");
    }
    emit changed();
}
