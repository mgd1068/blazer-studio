#include "grbl.h"
#include "geometry.h"
#include <QRegularExpression>
#include <cmath>
namespace {
QString n(double v) {
    return QString::number(v, 'f', 3);
}
bool point(const QByteArray &v, QPointF &p) {
    auto parts = v.split(',');
    if (parts.size() < 2)
        return false;
    bool a, b;
    double x = parts[0].toDouble(&a), y = parts[1].toDouble(&b);
    if (!a || !b || !std::isfinite(x) || !std::isfinite(y))
        return false;
    p = {x, y};
    return true;
}
} // namespace
GrblSession::GrblSession(QObject *parent) : QObject(parent) {
    poll.setInterval(200);
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    focusTimer.setSingleShot(true);
    focusTimer.setInterval(3000);
    connect(&serial, &QSerialPort::readyRead, this, &GrblSession::receive);
    connect(&serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e != QSerialPort::NoError && e != QSerialPort::NotOpenError)
            fail("Serieller Fehler: " + serial.errorString());
    });
    connect(&poll, &QTimer::timeout, this, [this] {
        if (connected())
            serial.write("?");
    });
    connect(&timeout, &QTimer::timeout, this,
            [this] { fail("GRBL antwortet nicht rechtzeitig; Übertragung abgebrochen."); });
    connect(&focusTimer, &QTimer::timeout, this, [this] { focus(false); });
}
void GrblSession::append(const QString &s) {
    log.prepend(s);
    while (log.size() > 30)
        log.removeLast();
    emit changed();
}
bool GrblSession::ready() const {
    return connected() && !handshaking && configurationValid() && knownPosition &&
           controllerState == "Idle" && !busy() && !firing;
}
bool GrblSession::open(const QString &port) {
    if (port.isEmpty() || connected())
        return false;
    serial.setPortName(port);
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial.open(QIODevice::ReadWrite)) {
        append(serial.errorString());
        return false;
    }
    input.clear();
    version = laserMode = powerScale = knownPosition = knownOffset = false;
    reportMm = coordinateSystem = bedWidth = bedHeight = false;
    controllerState.clear();
    firing = paused = false;
    progress = 0;
    handshaking = true;
    state = "Warte auf GRBL";
    timeout.start();
    serial.write("\r\n");
    QTimer::singleShot(2000, this, [this] {
        if (connected() && handshaking && !waitingAck)
            handshake();
    });
    poll.start();
    append("115200 Baud geöffnet; GRBL 1.1, $30=1000 und $32=1 werden geprüft.");
    return true;
}
void GrblSession::handshake() {
    paused = false;
    firing = false;
    homing = false;
    progress = 0;
    focusTimer.stop();
    timeout.stop();
    reportMm = coordinateSystem = bedWidth = bedHeight = false;
    queue = {"M5 S0", "$I", "$$", "$G"};
    total = queue.size();
    acked = 0;
    waitingAck = waitingIdle = false;
    handshaking = true;
    state = "GRBL prüfen";
    pump();
}
void GrblSession::close() {
    const bool active = busy() || firing;
    poll.stop();
    timeout.stop();
    focusTimer.stop();
    queue.clear();
    waitingAck = waitingIdle = handshaking = false;
    firing = paused = false;
    if (serial.isOpen()) {
        if (active)
            serial.write("!\x18", 2);
        else
            serial.write("M5 S0\n");
        serial.flush();
        serial.close();
    }
    state = "Getrennt";
    append("Verbindung geschlossen; nach Kommunikationsverlust ist der Ausgangszustand nicht "
           "bestätigt.");
}
void GrblSession::fail(const QString &message) {
    poll.stop();
    timeout.stop();
    focusTimer.stop();
    queue.clear();
    waitingAck = waitingIdle = handshaking = false;
    firing = paused = false;
    version = laserMode = powerScale = knownPosition = false;
    if (serial.isOpen()) {
        serial.write("!\x18", 2);
        serial.flush();
        serial.close();
    }
    state = "Verbindungsfehler";
    append(message);
}
void GrblSession::receive() {
    input += serial.readAll();
    if (input.size() > 65536) {
        fail("Ungültig lange GRBL-Antwort.");
        return;
    }
    int end;
    while ((end = input.indexOf('\n')) >= 0) {
        auto s = input.left(end).trimmed();
        input.remove(0, end + 1);
        if (!s.isEmpty())
            line(s);
    }
}
void GrblSession::line(const QByteArray &s) {
    if (s.startsWith("Grbl ")) {
        version = s.startsWith("Grbl 1.1") || s.startsWith("Grbl 1.3a");
        if (!version) {
            fail("Nur GRBL 1.1 und der Blaze-X-ESP32-Fork 1.3a sind unterstützt.");
            return;
        }
        laserMode = powerScale = knownPosition = knownOffset = false;
        handshake();
        return;
    }
    if (s.startsWith("[VER:"))
        version = s.startsWith("[VER:1.1") || s.startsWith("[VER:1.3a");
    if (s.startsWith("$13="))
        reportMm = s.mid(4).trimmed() == "0";
    if (s.startsWith("$130="))
        bedWidth = std::abs(s.mid(5).toDouble() - 200) < .0001;
    if (s.startsWith("$131="))
        bedHeight = std::abs(s.mid(5).toDouble() - 150) < .0001;
    if (s.startsWith("[GC:"))
        coordinateSystem = QRegularExpression("(?:^| )G54(?: |$)")
                               .match(QString::fromLatin1(s.mid(4, s.size() - 5)))
                               .hasMatch();
    if (s.startsWith("$30="))
        powerScale = std::abs(s.mid(4).toDouble() - 1000) < .0001;
    if (s.startsWith("$32="))
        laserMode = s.mid(4).trimmed() == "1";
    if (s.startsWith("$22="))
        homing = s.mid(4).trimmed() == "1";
    if (s == "error:9" && handshaking && !queue.isEmpty() && queue.first().startsWith("M5")) {
        line("ok");
        return;
    }
    if (s.startsWith("ALARM:") || s.startsWith("error:")) {
        fail(QString::fromLatin1(s));
        return;
    }
    if (s.startsWith('<') && s.endsWith('>')) {
        auto fields = s.mid(1, s.size() - 2).split('|');
        controllerState = QString::fromLatin1(fields.first()).section(':', 0, 0);
        QPointF machine, work;
        bool hasMachine = false, hasWork = false;
        for (const auto &field : fields) {
            if (field.startsWith("WCO:"))
                knownOffset = point(field.mid(4), offset);
            if (field.startsWith("MPos:"))
                hasMachine = point(field.mid(5), machine);
            if (field.startsWith("WPos:"))
                hasWork = point(field.mid(5), work);
        }
        if (hasWork) {
            position = work;
            knownPosition = true;
        } else if (hasMachine && knownOffset) {
            position = machine - offset;
            knownPosition = true;
        }
        if (controllerState == "Alarm" || controllerState == "Door") {
            queue.clear();
            waitingAck = waitingIdle = false;
            timeout.stop();
            firing = false;
            state = controllerState;
            append("Controller gesperrt: " + controllerState);
            return;
        }
        if (waitingIdle && controllerState == "Idle") {
            waitingIdle = false;
            timeout.stop();
            progress = 1;
            firing = false;
            state = "Bereit";
            append("Controller meldet Idle: Auftrag abgeschlossen.");
        }
        if (!busy() && !handshaking)
            state = configurationValid() && knownPosition
                        ? controllerState == "Idle" ? "Bereit" : controllerState
                        : "GRBL-Konfiguration nicht freigegeben";
        if (executing() && !firing && controllerState == "Hold") {
            paused = true;
            state = "Pausiert";
            timeout.stop();
        } else if (executing() && paused && controllerState == "Run") {
            paused = false;
            state = "Läuft";
            pump();
        }
        if (busy() && !paused &&
            (controllerState == "Run" || controllerState == "Jog" || controllerState == "Hold" ||
             controllerState == "Home"))
            timeout.start();
        emit changed();
        return;
    }
    if (s == "ok" && waitingAck) {
        if (--pendingAcks > 0)
            return;
        waitingAck = false;
        timeout.stop();
        if (!queue.isEmpty())
            queue.removeFirst();
        ++acked;
        progress = total ? std::min(.99, double(acked) / total) : 0;
        if (queue.isEmpty()) {
            if (handshaking) {
                handshaking = false;
                state = configurationValid() ? "Warte auf Position"
                                             : "GRBL-Konfiguration nicht freigegeben";
                serial.write("?");
            } else {
                waitingIdle = true;
                serial.write("?");
                timeout.start(30000);
            }
        } else
            pump();
        emit changed();
        return;
    }
    if (s.startsWith("[MSG:"))
        append(QString::fromLatin1(s));
}
void GrblSession::pump() {
    if (!connected() || waitingAck || paused || queue.isEmpty())
        return;
    auto bytes = queue.first().toLatin1() + "\n";
    if (bytes.size() > 79) {
        fail("GCode-Zeile länger als GRBL-Zeilenpuffer.");
        return;
    }
    if (serial.write(bytes) != bytes.size()) {
        fail("Serieller Schreibfehler.");
        return;
    }
    pendingAcks = bytes.count('\n');
    waitingAck = true;
    timeout.start();
}
bool GrblSession::commands(const QStringList &lines) {
    if (!ready() || lines.isEmpty()) {
        append("Aktion abgelehnt: GRBL nicht im geprüften Leerlauf.");
        return false;
    }
    queue = lines;
    total = queue.size();
    acked = 0;
    progress = 0;
    state = "Läuft";
    pump();
    emit changed();
    return connected() && busy();
}
void GrblSession::start(const JobPlan &job) {
    if (position.x() < 0 || position.y() < 0 || position.x() > 200 || position.y() > 150) {
        append("Aktuelle Arbeitsposition außerhalb des Arbeitsraums; Nullpunkt prüfen.");
        return;
    }
    if (!job.valid()) {
        append(job.errors.join("; "));
        return;
    }
    commands(job.gcode());
}
void GrblSession::pause() {
    if (!connected() || !busy() || firing || handshaking)
        return;
    paused = !paused;
    if (paused) {
        serial.write("!");
        timeout.stop();
        state = "Pausiert";
    } else {
        serial.write("~");
        state = "Läuft";
        if (waitingAck)
            timeout.start();
        pump();
    }
    emit changed();
}
void GrblSession::stop() {
    if (!connected())
        return;
    focusTimer.stop();
    firing = false;
    paused = false;
    queue.clear();
    waitingAck = waitingIdle = handshaking = false;
    timeout.stop();
    version = laserMode = powerScale = knownPosition = false;
    state = "Abbruch · warte auf GRBL-Neustart";
    serial.write("!\x18", 2);
    timeout.start();
    append("Feed Hold und Soft-Reset gesendet; keine automatische Fortsetzung.");
}
void GrblSession::jog(double dx, double dy) {
    auto target = position + QPointF(dx, dy);
    if (position.x() < 0 || position.y() < 0 || position.x() > 200 || position.y() > 150 ||
        !std::isfinite(dx) || !std::isfinite(dy) || target.x() < 0 || target.y() < 0 ||
        target.x() > 200 || target.y() > 150) {
        append("Bewegung außerhalb des Arbeitsraums abgelehnt.");
        return;
    }
    commands({"M5", "$J=G91 G21 X" + n(dx) + " Y" + n(dy) + " F600"});
}
void GrblSession::home() {
    if (!homing) {
        append("Referenzfahrt nicht aktiviert ($22).");
        return;
    }
    if (connected() && controllerState == "Alarm" && !busy() && version && laserMode &&
        powerScale && reportMm && bedWidth && bedHeight) {
        queue = {"$H"};
        total = 1;
        acked = 0;
        progress = 0;
        paused = false;
        state = "Referenzfahrt";
        pump();
        emit changed();
    } else
        commands({"M5", "$H"});
}
void GrblSession::zero() {
    if (commands({"M5", "G10 L20 P1 X0 Y0", "G54"})) {
        knownPosition = false;
        knownOffset = false;
    }
}
void GrblSession::frame(const QVariantMap &b) {
    QRectF r(b.value("x").toDouble(), b.value("y").toDouble(), b.value("width").toDouble(),
             b.value("height").toDouble());
    if (!b.value("valid").toBool() || !Geometry::insideBed(r)) {
        append("Ungültige Rahmenfahrt.");
        return;
    }
    QStringList lines{"M5", "G21", "G90", "G54"};
    for (auto p : {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()})
        lines << "G0 X" + n(p.x()) + " Y" + n(p.y());
    commands(lines);
}
void GrblSession::focus(bool on) {
    if (!on) {
        if (firing)
            stop();
        return;
    }
    if (!ready())
        return;
    // A stationary GRBL M3 output has no independent watchdog. Host deadline and
    // release reset it; communication loss cannot guarantee output shutdown.
    if (position.x() < 0 || position.y() < 0 || position.x() > 200 || position.y() > 150)
        return;
    // Send the finite pulse and its shutdown together, within the 128-byte RX buffer.
    if (!commands({"M5\nG21\nG90\nG1 F1 M3 S20\nG4 P0.2\nM5 S0\nG0"}))
        return;
    firing = true;
    focusTimer.start();
    emit changed();
}
