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
bool Simulator::setDeviceProfile(const DeviceProfile &profile) {
    if (busy() || firing())
        return false;
    if (profile.outputFingerprint() == m_deviceProfile.outputFingerprint())
        return true;
    m_deviceProfile = profile;
    m_job = JobPlan(profile);
    m_x = profile.workArea().width() / 2;
    m_y = profile.workArea().height() / 2;
    m_frame = {};
    m_framePath.clear();
    m_frameTravelLength = 0;
    m_progress = 0;
    emit changed();
    return true;
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
    m_framePath.clear();
    m_frameTravelLength = 0;
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
    m_x = std::clamp(m_x + dx, 0., m_deviceProfile.workArea().width());
    m_y = std::clamp(m_y + dy, 0., m_deviceProfile.workArea().height());
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
    append(on ? QString::number(m_deviceProfile.driverSettings()["focusPower"].toDouble()) +
                    " % Fokuspunkt simuliert · max. 3 Sekunden"
              : "Laser-Aus simuliert");
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
    if (!bounds["valid"].toBool() || !Geometry::insideBed(rect, m_deviceProfile.workArea())) {
        append("Rahmenfahrt abgelehnt: kein aktives Motiv oder außerhalb des Arbeitsraums.");
        return;
    }
    m_frame = rect;
    const QPointF start(m_x, m_y);
    m_framePath = {start, rect.topLeft(), rect.topRight(), rect.bottomRight(),
                   rect.bottomLeft(), rect.topLeft(), start};
    m_frameTravelLength = 0;
    for (qsizetype i = 1; i < m_framePath.size(); ++i)
        m_frameTravelLength += QLineF(m_framePath[i - 1], m_framePath[i]).length();
    m_mode = Mode::Framing;
    m_progress = 0;
    m_elapsedMs = 0;
    m_clock.start();
    m_tick.start();
    // Keep the actual head at its known start until the first interpolated tick.
    append("Rahmenfahrt mit laserfreier Anfahrt und Rückfahrt simuliert · ohne Emission");
}
void Simulator::start() {
    if (m_mode != Mode::Ready || m_firing)
        return;
    m_mode = Mode::Running;
    m_progress = 0;
    m_elapsedMs = 0;
    m_clock.start();
    m_tick.start();
    append(m_job.valid() ? "Berechnete Laserpfade simulieren · Zeitraffer 16 Sekunden"
                         : "Demoablauf gestartet · kein berechneter Maschinenjob");
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
        double remaining = m_progress * m_frameTravelLength;
        auto point = m_framePath.last();
        for (qsizetype i = 1; i < m_framePath.size(); ++i) {
            const auto from = m_framePath[i - 1], to = m_framePath[i];
            const double length = QLineF(from, to).length();
            if (length <= 0) continue;
            if (remaining <= length) {
                point = from + (to - from) * std::clamp(remaining / length, 0., 1.);
                break;
            }
            remaining -= length;
        }
        if (m_progress >= 1) point = m_framePath.first(); // Exact only on normal completion.
        m_x = point.x();
        m_y = point.y();
    } else if (m_job.valid()) {
        double time = m_progress * m_job.seconds;
        for (const auto &move : m_job.moves) {
            double seconds = QLineF(move.from, move.to).length() / move.speed * 60.;
            if (time <= seconds) {
                auto p = move.from + (move.to - move.from) *
                                         (seconds > 0 ? std::clamp(time / seconds, 0., 1.) : 1.);
                m_x = p.x();
                m_y = p.y();
                break;
            }
            time -= seconds;
            m_x = move.to.x();
            m_y = move.to.y();
        }
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
