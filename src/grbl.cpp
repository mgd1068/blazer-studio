#include "grbl.h"
#include "geometry.h"
#include <QMap>
#include <QRegularExpression>
#include <QScopedValueRollback>
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
           knownOutput && reportedOutput == 0 && !activeInputs &&
           (!overrideFeedback || powerOverride <= 100) && controllerState == "Idle" && !busy() &&
           !firing;
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
    minimumPower = knownOutput = activeInputs = pauseRequested = false;
    contaminatedConfiguration = false;
    configurationRetries = 0;
    overrideFeedback = false;
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
    // Settings printouts must not be interrupted by our status polling. Some
    // ESP32 forks can otherwise interleave two replies on the serial stream.
    poll.stop();
    paused = false;
    firing = false;
    homing = false;
    progress = 0;
    focusTimer.stop();
    timeout.stop();
    reportMm = coordinateSystem = bedWidth = bedHeight = false;
    laserMode = powerScale = minimumPower = knownPosition = knownOffset = knownOutput = false;
    activeInputs = pauseRequested = contaminatedConfiguration = false;
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    overrideFeedback = false;
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
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    firing = paused = false;
    pauseRequested = false;
    knownPosition = knownOffset = knownOutput = false;
    overrideFeedback = false;
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
    // A write/flush on a lost USB port can synchronously emit another serial
    // error. Do not recurse while attempting the best-effort shutdown.
    if (handlingFailure)
        return;
    QScopedValueRollback<bool> failureGuard(handlingFailure, true);
    poll.stop();
    timeout.stop();
    focusTimer.stop();
    queue.clear();
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    firing = paused = false;
    pauseRequested = false;
    knownOutput = knownOffset = false;
    overrideFeedback = false;
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
        if (!connected()) {
            input.clear();
            return;
        }
    }
}
void GrblSession::line(const QByteArray &s) {
    if (s.contains('<') &&
        (!s.startsWith('<') || !s.endsWith('>') || s.count('<') > 1)) {
        QByteArray remainder = s;
        QList<QByteArray> reports;
        int begin;
        while ((begin = remainder.indexOf('<')) >= 0) {
            const int end = remainder.indexOf('>', begin);
            if (end < 0)
                break;
            reports.append(remainder.mid(begin, end - begin + 1));
            remainder.remove(begin, end - begin + 1);
        }
        if (!reports.isEmpty()) {
            const bool taintedSetting = handshaking && remainder.trimmed().startsWith('$');
            if (taintedSetting)
                contaminatedConfiguration = true;
            for (const auto &report : reports) {
                line(report);
                if (!connected())
                    return;
            }
            // A numeric setting may have been interrupted in the middle of its
            // value. Never authorize a configuration from that partial number.
            if (!taintedSetting && !remainder.trimmed().isEmpty())
                line(remainder.trimmed());
            return;
        }
    }
    if (s.startsWith("Grbl ")) {
        version = s.startsWith("Grbl 1.1") || s.startsWith("Grbl 1.3a");
        if (!version) {
            fail("Nur GRBL 1.1 und der Blaze-X-ESP32-Fork 1.3a sind unterstützt.");
            return;
        }
        laserMode = powerScale = knownPosition = knownOffset = false;
        configurationRetries = 0;
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
    if (s.startsWith("$31=")) {
        bool valid;
        const double value = s.mid(4).toDouble(&valid);
        minimumPower = valid && std::isfinite(value) && value == 0;
    }
    if (s.startsWith("$32="))
        laserMode = s.mid(4).trimmed() == "1";
    if (s.startsWith("$22="))
        homing = s.mid(4).trimmed() == "1";
    if (s == "error:9" && handshaking && !queue.isEmpty() && queue.first().startsWith("M5")) {
        line("ok");
        return;
    }
    if (s.startsWith("ALARM:") || s.startsWith("error:")) {
        static const QMap<int, QString> errors{{1, "Befehl nicht erkannt"},
                                               {2, "Zahlenformat ungültig"},
                                               {9, "Controller durch Alarm gesperrt"},
                                               {15, "Bewegungsgrenze überschritten"},
                                               {20, "GCode-Befehl nicht unterstützt"},
                                               {22, "Vorschub fehlt"},
                                               {33, "Ungültige Bogenbewegung"}};
        static const QMap<int, QString> alarms{{1, "Endschalter ausgelöst"},
                                               {2, "Softwaregrenze überschritten"},
                                               {3, "Reset während Bewegung: Position prüfen"},
                                               {8, "Referenzschalter wurde nicht freigegeben"},
                                               {9, "Referenzschalter wurde nicht gefunden"}};
        int code = s.mid(s.indexOf(':') + 1).toInt();
        auto message = s.startsWith("ALARM:") ? alarms.value(code, "Controlleralarm")
                                              : errors.value(code, "Controllerfehler");
        fail(QString::fromLatin1(s) + " · " + message);
        return;
    }
    if (s.startsWith('<') && s.endsWith('>')) {
        auto fields = s.mid(1, s.size() - 2).split('|');
        controllerState = QString::fromLatin1(fields.first()).section(':', 0, 0);
        QPointF machine, work;
        bool hasMachine = false, hasWork = false;
        knownPosition = knownOutput = activeInputs = false;
        for (const auto &field : fields) {
            if (field.startsWith("FS:")) {
                const auto values = field.mid(3).split(',');
                if (values.size() == 2) {
                    bool validFeed, validOutput;
                    const double feed = values[0].toDouble(&validFeed);
                    reportedOutput = values[1].toDouble(&validOutput);
                    knownOutput = validFeed && validOutput && std::isfinite(feed) && feed >= 0 &&
                                  std::isfinite(reportedOutput) && reportedOutput >= 0;
                }
            }
            if (field.startsWith("Pn:"))
                activeInputs = field.size() > 3;
            if (field.startsWith("Ov:")) {
                auto values = field.mid(3).split(',');
                if (values.size() == 3) {
                    feedOverride = values[0].toInt();
                    powerOverride = values[2].toInt();
                    overrideFeedback = feedOverride >= 10 && feedOverride <= 200 &&
                                       powerOverride >= 10 && powerOverride <= 200;
                }
            }
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
            if (executing() || firing) {
                fail("Controller gesperrt während Auftrag: " + controllerState +
                     ". Auftrag verworfen; Ausgangszustand prüfen.");
                return;
            }
            queue.clear();
            inFlight.clear();
            sent = rxBytes = 0;
            buffering = false;
            waitingAck = waitingIdle = false;
            timeout.stop();
            firing = false;
            paused = pauseRequested = false;
            state = controllerState;
            append("Controller gesperrt: " + controllerState);
            return;
        }
        if (controllerState == "Idle" && knownOutput && reportedOutput != 0 && !firing) {
            fail("Controller meldet Leistung im Leerlauf; Auftrag verworfen, Ausgang prüfen.");
            return;
        }
        if (executing() && overrideFeedback && powerOverride > 100) {
            fail("Leistungs-Override über 100 %; Auftrag verworfen, Ausgang prüfen.");
            return;
        }
        if (waitingIdle && controllerState == "Idle" && knownPosition && knownOutput &&
            reportedOutput == 0 && !activeInputs) {
            waitingIdle = false;
            timeout.stop();
            progress = 1;
            firing = false;
            paused = pauseRequested = false;
            state = "Bereit";
            append("Controller meldet Idle: Auftrag abgeschlossen.");
        }
        if (!busy() && !handshaking) {
            if (!configurationValid())
                state = "GRBL-Konfiguration nicht freigegeben";
            else if (!knownPosition || !knownOutput)
                state = "Positions-/Leistungsrückmeldung fehlt";
            else if (activeInputs)
                state = "Controller-Eingang aktiv";
            else if (overrideFeedback && powerOverride > 100)
                state = "Leistungs-Override über 100 %";
            else
                state = controllerState == "Idle" ? "Bereit" : controllerState;
        }
        if (executing() && !firing && controllerState == "Hold") {
            paused = true;
            state = "Pausiert";
            timeout.stop();
        } else if (executing() && paused && !pauseRequested && controllerState == "Run") {
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
        if (buffering) {
            if (inFlight.isEmpty()) {
                fail("Unerwartete GRBL-Quittierung.");
                return;
            }
            rxBytes -= inFlight.takeFirst();
            --sent;
        } else if (--pendingAcks > 0)
            return;
        waitingAck = buffering && !inFlight.isEmpty();
        timeout.stop();
        if (!queue.isEmpty())
            queue.removeFirst();
        ++acked;
        progress = total ? std::min(.99, double(acked) / total) : 0;
        if (queue.isEmpty()) {
            if (handshaking) {
                if (contaminatedConfiguration) {
                    if (configurationRetries++ == 0) {
                        append("Verschachtelte Konfigurationsantwort; lese Einstellungen erneut.");
                        handshake();
                    } else
                        fail("Konfiguration nicht eindeutig lesbar; Verbindung geschlossen.");
                    return;
                }
                handshaking = false;
                state = configurationValid() ? "Warte auf Position"
                                             : "GRBL-Konfiguration nicht freigegeben";
                serial.write("?");
                poll.start();
            } else {
                waitingIdle = true;
                serial.write("?");
                if (!paused)
                    timeout.start(30000);
                else
                    timeout.stop();
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
    if (!connected() || paused || queue.isEmpty() || (!buffering && waitingAck))
        return;
    do {
        int index = buffering ? sent : 0;
        if (index >= queue.size())
            break;
        auto bytes = queue[index].toLatin1() + "\n";
        if (bytes.size() > 79) {
            fail("GCode-Zeile länger als GRBL-Zeilenpuffer.");
            return;
        }
        if (buffering && rxBytes + bytes.size() > 100)
            break;
        if (serial.write(bytes) != bytes.size()) {
            fail("Serieller Schreibfehler.");
            return;
        }
        if (buffering) {
            inFlight.append(bytes.size());
            rxBytes += bytes.size();
            ++sent;
        } else
            pendingAcks = bytes.count('\n');
        waitingAck = true;
        timeout.start();
    } while (buffering);
}
bool GrblSession::commands(const QStringList &lines, bool buffered) {
    if (!ready() || lines.isEmpty()) {
        append("Aktion abgelehnt: GRBL nicht im geprüften Leerlauf.");
        return false;
    }
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = buffered;
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
    commands(job.gcode(), true);
}
void GrblSession::pause() {
    if (!connected() || !busy() || firing || handshaking)
        return;
    paused = !paused;
    pauseRequested = paused;
    if (paused) {
        serial.write("!");
        timeout.stop();
        state = "Pausiert";
    } else {
        serial.write("~");
        state = "Läuft";
        if (waitingAck || waitingIdle)
            timeout.start(waitingIdle ? 30000 : 10000);
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
    pauseRequested = false;
    knownOutput = false;
    queue.clear();
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    timeout.stop();
    version = laserMode = powerScale = knownPosition = false;
    state = "Abbruch · warte auf GRBL-Neustart";
    serial.write("!\x18", 2);
    if (!connected())
        return;
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

bool GrblSession::overrides(int feed, int power) {
    if (!connected() || !configurationValid() || !overrideFeedback || feed < 10 || feed > 200 ||
        power < 10 || power > 100)
        return false;
    QByteArray bytes;
    auto change = [&](int current, int target, int reset, int plus10, int minus10, int plus1,
                      int minus1) {
        if (target == 100) {
            bytes.append(char(reset));
            return;
        }
        int delta = target - current;
        while (std::abs(delta) >= 10) {
            bytes.append(char(delta > 0 ? plus10 : minus10));
            delta += delta > 0 ? -10 : 10;
        }
        while (delta) {
            bytes.append(char(delta > 0 ? plus1 : minus1));
            delta += delta > 0 ? -1 : 1;
        }
    };
    change(feedOverride, feed, 0x90, 0x91, 0x92, 0x93, 0x94);
    change(powerOverride, power, 0x99, 0x9a, 0x9b, 0x9c, 0x9d);
    serial.write(bytes);
    serial.write("?");
    return true;
}
