#include "grbl.h"
#include "geometry.h"
#include <QMap>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSignalBlocker>
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
GrblSession::GrblSession(QObject *parent, DeviceProfile profile)
    : ControllerSession(parent), m_deviceProfile(std::move(profile)) {
    poll.setInterval(200);
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    focusTimer.setSingleShot(true);
    focusTimer.setInterval(3000);
    connect(&serial, &QSerialPort::readyRead, this, &GrblSession::receive);
    connect(&serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e != QSerialPort::NoError && e != QSerialPort::NotOpenError)
            fail("Serieller Fehler: " + serial.errorString(),
                 ControllerDiagnostics::FaultCause::Transport);
    });
    connect(&poll, &QTimer::timeout, this, [this] {
        if (connected()) {
            diagnostic.statusQuery(serial.write("?") == 1);
            // Expiry must also update the UI when a controller goes silent.
            if (!statusFresh())
                emit changed();
        }
    });
    connect(&timeout, &QTimer::timeout, this, [this] {
        fail("GRBL antwortet nicht rechtzeitig; Übertragung abgebrochen.",
             ControllerDiagnostics::FaultCause::Timeout);
    });
    connect(&focusTimer, &QTimer::timeout, this, [this] { focus(false); });
}
GrblSession::~GrblSession() {
    if (connected()) {
        // Use the same best-effort shutdown as explicit disconnect, while serial
        // and timers still exist. Consumers must not re-enter a dying session.
        const QSignalBlocker notifications(this);
        close();
    }
}
void GrblSession::append(const QString &s) {
    log.prepend(s);
    while (log.size() > 30)
        log.removeLast();
    emit changed();
}
ControllerDiagnostics::LiveState GrblSession::diagnosticState() const {
    return {connected(),
            ready(),
            verified(),
            knownPosition,
            knownOffset,
            knownOutput,
            positionReferenced,
            commissionedHomingContract,
            commissionedEnvelope.has_value(),
            commissionedModalContract,
            connected() && knownPosition && knownOffset && knownOutput && reportMm &&
                !awaitingReset && !handshaking && statusFresh(),
            lastStatus.isValid() ? lastStatus.elapsed() : -1,
            knownOutput ? std::optional<double>(reportedOutput) : std::nullopt};
}
QVariantMap GrblSession::diagnosticSnapshot() const {
    return diagnostic.snapshot(diagnosticState());
}
ControllerDiagnostics::Denial GrblSession::diagnosticMotionDenial() const {
    if (!commissionedEnvelope || !commissionedModalContract)
        return ControllerDiagnostics::Denial::MotionContract;
    if (!positionReferenced)
        return ControllerDiagnostics::Denial::ReferenceMissing;
    return ControllerDiagnostics::Denial::StaleStatus;
}
void GrblSession::auditDispatch(const QByteArray &bytes) {
    using C = ControllerDiagnostics::Command;
    for (auto block : bytes.split('\n')) {
        block = block.trimmed();
        if (block.isEmpty())
            continue;
        C kind = C::Unretained;
        std::optional<QPointF> target;
        std::optional<double> requestedPower;
        if (!block.startsWith('(')) {
            // Classify only known outgoing words. Neither the raw command nor
            // arbitrary comment/payload bytes enter the typed audit buffer.
            static const QRegularExpression word(
                "(?:^| )([GMSXY])([+-]?[0-9]+(?:\\.[0-9]+)?)(?= |$)");
            auto matches = word.globalMatch(QString::fromLatin1(block));
            std::optional<double> x, y;
            bool rapid = false, feed = false;
            while (matches.hasNext()) {
                const auto match = matches.next();
                bool valid;
                const auto number = match.captured(2).toDouble(&valid);
                if (!valid || !std::isfinite(number))
                    continue;
                const auto letter = match.captured(1);
                if (letter == "M") {
                    if (number == 5)
                        diagnosticSpindle = 0;
                    else if (number == 3 || number == 4)
                        diagnosticSpindle = int(number);
                } else if (letter == "S")
                    diagnosticPower = number;
                else if (letter == "X")
                    x = number;
                else if (letter == "Y")
                    y = number;
                else if (letter == "G") {
                    rapid |= number == 0;
                    feed |= number == 1;
                }
            }
            if (x && y)
                target = QPointF(*x, *y);
            if (block == "M5" || block == "M5 S0")
                kind =
                    diagnostic.jobActive() && block == "M5 S0" ? C::FinalSafetyOff : C::SafetyOff;
            else if (block == "$I")
                kind = C::Identify;
            else if (block == "$$")
                kind = C::ReadSettings;
            else if (block == "$G")
                kind = C::ReadModal;
            else if (block == "$H")
                kind = C::Home;
            else if (block.startsWith("$J="))
                kind = C::Jog;
            else if (block.startsWith("G10 L20 P1"))
                kind = C::WorkZero;
            else if (block == "G21")
                kind = C::UnitsMm;
            else if (block == "G90")
                kind = C::Absolute;
            else if (block == "G54")
                kind = C::G54;
            else if (block == "G17")
                kind = C::PlaneXY;
            else if (block == "G94")
                kind = C::FeedMode;
            else if (block == "S0")
                kind = C::PowerZero;
            else if (block.startsWith("M4 ")) {
                kind = C::DynamicPowerRequested;
                requestedPower = diagnosticPower;
            } else if (block.contains("M3 S")) {
                kind = C::ConstantPowerRequested;
                requestedPower = diagnosticPower;
            } else if (block.startsWith("G4 "))
                kind = C::Dwell;
            if (rapid && target && diagnostic.wantsApproach())
                kind = C::FirstApproachRequested;
            else if (feed && target && diagnosticSpindle != 0 && diagnosticPower > 0 &&
                     diagnostic.wantsPoweredTarget()) {
                kind = C::FirstPoweredTargetRequested;
                requestedPower = diagnosticPower;
            } else if (kind != C::FirstApproachRequested)
                target.reset();
        }
        diagnostic.queued(kind, target, requestedPower);
    }
}
bool GrblSession::statusFresh() const {
    return lastStatus.isValid() && lastStatus.elapsed() <= 1500;
}
QString GrblSession::motionSafetyError() const {
    if (!commissionedEnvelope || !commissionedModalContract)
        return "Reale Ausgabe gesperrt: Maschinenkoordinaten, Fahrgrenzen und G54-Zuordnung "
               "sind für diesen Controller nicht hardwareverifiziert. Referenzfahrt allein "
               "bestätigt diese Zuordnung nicht.";
    if (!positionReferenced)
        return "Reale Ausgabe gesperrt: bestätigte Maschinenreferenz fehlt.";
    if (!statusFresh())
        return "Reale Ausgabe gesperrt: aktuelle Controllerposition nicht frisch bestätigt.";
    return {};
}
QString GrblSession::homingSafetyError() const {
    if (!commissionedHomingContract)
        return "Reale Referenzfahrt gesperrt: Endschalter, Homing-Richtung und Parameter "
               "sind für diesen Controller nicht gesondert verifiziert. $22=1 allein "
               "ist keine Freigabe für Bewegung.";
    if (!homing)
        return "Referenzfahrt nicht aktiviert ($22).";
    if (busy() || firing)
        return "Referenzfahrt gesperrt: eine andere Controlleraktion ist aktiv.";
    const bool alarmReady = connected() && controllerState == "Alarm" && statusFresh() &&
                            knownOutput && reportedOutput == 0 && !activeInputs && version &&
                            laserMode && powerScale && reportMm && bedWidth && bedHeight;
    if (!ready() && !alarmReady)
        return "Referenzfahrt gesperrt: aktueller geprüfter Leerlauf oder Alarmstatus mit "
               "gemeldetem Ausgang 0 fehlt.";
    return {};
}
bool GrblSession::ready() const {
    return statusFresh() && connected() && !handshaking && configurationValid() && knownPosition &&
           knownOffset && knownOutput && reportedOutput == 0 && !activeInputs &&
           (!overrideFeedback || powerOverride <= 100) && controllerState == "Idle" && !busy() &&
           !firing;
}
bool GrblSession::insideMachineBed(const QPointF &workPoint) const {
    if (!commissionedEnvelope || !commissionedModalContract || !knownOffset ||
        !std::isfinite(workPoint.x()) || !std::isfinite(workPoint.y()))
        return false;
    // Match the three-decimal coordinates actually sent in GCode. G54 changes
    // the work origin, not the physical travel available to the machine.
    const QPointF machinePoint{std::round(workPoint.x() * 1000) / 1000 + offset.x(),
                               std::round(workPoint.y() * 1000) / 1000 + offset.y()};
    constexpr double epsilon = .0000001;
    return std::isfinite(machinePoint.x()) && std::isfinite(machinePoint.y()) &&
           machinePoint.x() >= commissionedEnvelope->left() - epsilon &&
           machinePoint.y() >= commissionedEnvelope->top() - epsilon &&
           machinePoint.x() <= commissionedEnvelope->right() + epsilon &&
           machinePoint.y() <= commissionedEnvelope->bottom() + epsilon;
}
bool GrblSession::open(const QString &port) {
    if (port.isEmpty() || connected()) {
        diagnostic.deny(ControllerDiagnostics::Action::Dispatch,
                        ControllerDiagnostics::Denial::ControllerNotReady);
        return false;
    }
    const auto profileError = DeviceBackends::runtimeSupportError(m_deviceProfile);
    if (!profileError.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Dispatch,
                        ControllerDiagnostics::Denial::OutputContract);
        append(profileError);
        return false;
    }
    serial.setPortName(port);
    serial.setBaudRate(m_deviceProfile.driverSettings()["baudRate"].toInt());
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial.open(QIODevice::ReadWrite)) {
        diagnostic.deny(ControllerDiagnostics::Action::Dispatch,
                        ControllerDiagnostics::Denial::ControllerNotReady);
        append(serial.errorString());
        return false;
    }
    input.clear();
    version = laserMode = powerScale = knownPosition = knownOffset = false;
    minimumPower = knownOutput = activeInputs = pauseRequested = zeroPending = false;
    contaminatedConfiguration = false;
    configurationRetries = 0;
    overrideFeedback = false;
    reportMm = coordinateSystem = bedWidth = bedHeight = false;
    controllerState.clear();
    firing = paused = false;
    progress = 0;
    handshaking = true;
    awaitingReset = true;
    connectionVerified = false;
    state = "Controller zurücksetzen";
    poll.stop();
    timeout.start(10000);
    // Realtime hold/reset discards a previous controller queue, never resumes it.
    // No normal command or old acknowledgement may authorize this connection.
    diagnostic.resetRequested(ControllerDiagnostics::ResetReason::Connect);
    const bool resetQueued = serial.write("!\x18", 2) == 2;
    diagnostic.resetWriteAccepted(resetQueued);
    if (!resetQueued) {
        fail("Controller-Reset konnte nicht gesendet werden.",
             ControllerDiagnostics::FaultCause::WriteFailure);
        return false;
    }
    append(
        QString("%1 Baud geöffnet; Controller-Reset, danach GRBL, $30=%2 und $32=1 frisch prüfen.")
            .arg(m_deviceProfile.driverSettings()["baudRate"].toInt())
            .arg(m_deviceProfile.driverSettings()["powerScale"].toDouble(), 0, 'f', 0));
    return true;
}
void GrblSession::handshake() {
    lastStatus.invalidate();
    commissionedModalContract = commissionedHomingContract = false;
    positionReferenced = homingPending = false;
    awaitingReset = false;
    connectionVerified = false;
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
    activeInputs = pauseRequested = contaminatedConfiguration = zeroPending = false;
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
    commissionedModalContract = commissionedHomingContract = false;
    positionReferenced = homingPending = false;
    const bool active = busy() || firing;
    poll.stop();
    timeout.stop();
    focusTimer.stop();
    queue.clear();
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    awaitingReset = connectionVerified = false;
    firing = paused = false;
    pauseRequested = zeroPending = false;
    knownPosition = knownOffset = knownOutput = false;
    overrideFeedback = false;
    if (serial.isOpen()) {
        if (active) {
            serial.clear(QSerialPort::Output);
            diagnostic.resetRequested(ControllerDiagnostics::ResetReason::Disconnect);
            diagnostic.resetWriteAccepted(serial.write("!\x18", 2) == 2);
        } else {
            if (serial.write("M5 S0\n") == 6)
                auditDispatch("M5 S0\n");
        }
        serial.flush();
        serial.close();
    }
    diagnostic.disconnected();
    state = "Getrennt";
    append("Verbindung geschlossen; nach Kommunikationsverlust ist der Ausgangszustand nicht "
           "bestätigt.");
}
void GrblSession::fail(const QString &message, ControllerDiagnostics::FaultCause cause) {
    commissionedModalContract = commissionedHomingContract = false;
    positionReferenced = homingPending = false;
    // A write/flush on a lost USB port can synchronously emit another serial
    // error. Do not recurse while attempting the best-effort shutdown.
    if (handlingFailure)
        return;
    QScopedValueRollback<bool> failureGuard(handlingFailure, true);
    diagnostic.fault(cause);
    poll.stop();
    timeout.stop();
    focusTimer.stop();
    queue.clear();
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    awaitingReset = connectionVerified = false;
    firing = paused = false;
    pauseRequested = zeroPending = false;
    knownOutput = knownOffset = false;
    overrideFeedback = false;
    version = laserMode = powerScale = knownPosition = false;
    if (serial.isOpen()) {
        serial.clear(QSerialPort::Output);
        diagnostic.resetRequested(ControllerDiagnostics::ResetReason::Fault);
        diagnostic.resetWriteAccepted(serial.write("!\x18", 2) == 2);
        serial.flush();
        serial.close();
    }
    state = "Verbindungsfehler";
    append(message);
}
void GrblSession::receive() {
    input += serial.readAll();
    if (input.size() > 65536) {
        fail("Ungültig lange GRBL-Antwort.", ControllerDiagnostics::FaultCause::InvalidReply);
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
    if (s.contains('<') && (!s.startsWith('<') || !s.endsWith('>') || s.count('<') > 1)) {
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
    if (awaitingReset && !s.startsWith("Grbl "))
        return;
    if (s.startsWith("Grbl ")) {
        diagnostic.startupObserved();
        version = s.startsWith("Grbl 1.1") || s.startsWith("Grbl 1.3a");
        if (!version) {
            fail("Nur GRBL 1.1 und der Blaze-X-ESP32-Fork 1.3a sind unterstützt.",
                 ControllerDiagnostics::FaultCause::UnsupportedController);
            return;
        }
        laserMode = powerScale = knownPosition = knownOffset = false;
        configurationRetries = 0;
        handshake();
        return;
    }
    if (s.startsWith('$')) {
        const int equal = s.indexOf('=');
        if (equal > 1) {
            bool validKey, validValue;
            const auto key = s.mid(1, equal - 1).toInt(&validKey);
            const auto value = s.mid(equal + 1).trimmed().toDouble(&validValue);
            if (validKey)
                diagnostic.setting(key, validValue ? std::optional<double>(value) : std::nullopt);
        }
    }
    if (s.startsWith("[VER:"))
        version = s.startsWith("[VER:1.1") || s.startsWith("[VER:1.3a");
    if (s.startsWith("$13="))
        reportMm = s.mid(4).trimmed() == "0";
    if (s.startsWith("$130="))
        bedWidth = std::abs(s.mid(5).toDouble() - m_deviceProfile.workArea().width()) < .0001;
    if (s.startsWith("$131="))
        bedHeight = std::abs(s.mid(5).toDouble() - m_deviceProfile.workArea().height()) < .0001;
    if (s.startsWith("[GC:")) {
        int coordinateCode = -1, unitsCode = -1, distanceCode = -1;
        int coordinateCount = 0, unitsCount = 0, distanceCount = 0;
        if (s.endsWith(']')) {
            const auto tokens =
                QString::fromLatin1(s.mid(4, s.size() - 5)).split(' ', Qt::SkipEmptyParts);
            for (const auto &token : tokens) {
                if (!token.startsWith('G'))
                    continue;
                bool valid;
                const int code = token.mid(1).toInt(&valid);
                if (!valid)
                    continue;
                if (code >= 54 && code <= 59) {
                    coordinateCode = code;
                    ++coordinateCount;
                } else if (code == 20 || code == 21) {
                    unitsCode = code;
                    ++unitsCount;
                } else if (code == 90 || code == 91) {
                    distanceCode = code;
                    ++distanceCount;
                }
            }
        }
        diagnostic.reportedModal(coordinateCount == 1 ? coordinateCode : -1,
                                 unitsCount == 1 ? unitsCode : -1,
                                 distanceCount == 1 ? distanceCode : -1);
        coordinateSystem = QRegularExpression("(?:^| )G54(?: |$)")
                               .match(QString::fromLatin1(s.mid(4, s.size() - 5)))
                               .hasMatch();
        if (!coordinateSystem) {
            // Returning to G54 later cannot resurrect evidence established
            // before an unexpected coordinate-system change.
            commissionedModalContract = false;
            if (executing() || firing) {
                fail("Aktives Arbeitskoordinatensystem verändert; Auftrag verworfen.",
                     ControllerDiagnostics::FaultCause::CoordinateMismatch);
                return;
            }
        }
    }
    if (s.startsWith("$30="))
        powerScale = std::abs(s.mid(4).toDouble() -
                              m_deviceProfile.driverSettings()["powerScale"].toDouble()) < .0001;
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
        diagnostic.protocolFault(false, 9);
        diagnostic.rejected(9);
        QScopedValueRollback<bool> auditNormalization(diagnosticNormalizedAck, true);
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
        bool diagnosticCodeValid;
        s.mid(s.indexOf(':') + 1).toInt(&diagnosticCodeValid);
        diagnostic.protocolFault(s.startsWith("ALARM:"), diagnosticCodeValid ? code : -1);
        if (s.startsWith("error:") && waitingAck)
            diagnostic.rejected(diagnosticCodeValid ? code : -1);
        auto message = s.startsWith("ALARM:") ? alarms.value(code, "Controlleralarm")
                                              : errors.value(code, "Controllerfehler");
        fail(QString::fromLatin1(s) + " · " + message,
             ControllerDiagnostics::FaultCause::ControllerError);
        return;
    }
    if (s.startsWith('<') && s.endsWith('>')) {
        auto fields = s.mid(1, s.size() - 2).split('|');
        controllerState = QString::fromLatin1(fields.first()).section(':', 0, 0);
        const auto previousOffset = offset;
        const bool hadOffset = knownOffset;
        QPointF machine, work;
        bool hasMachine = false, hasWork = false, machineField = false, workField = false,
             offsetField = false;
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
            if (field.startsWith("WCO:")) {
                offsetField = true;
                knownOffset = point(field.mid(4), offset);
            }
            if (field.startsWith("MPos:")) {
                machineField = true;
                hasMachine = point(field.mid(5), machine);
            }
            if (field.startsWith("WPos:")) {
                workField = true;
                hasWork = point(field.mid(5), work);
            }
        }
        diagnostic.reportedOutput(knownOutput ? std::optional<double>(reportedOutput)
                                              : std::nullopt);
        using DS = ControllerDiagnostics::ControllerState;
        static const QMap<QString, DS> diagnosticStates{
            {"Idle", DS::Idle}, {"Run", DS::Run},     {"Home", DS::Home},
            {"Hold", DS::Hold}, {"Alarm", DS::Alarm}, {"Door", DS::Door},
            {"Jog", DS::Jog},   {"Check", DS::Check}, {"Sleep", DS::Sleep}};
        diagnostic.reportedState(diagnosticStates.value(controllerState, DS::Unknown));
        if (machineField)
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Machine,
                                  hasMachine && reportMm ? std::optional<QPointF>(machine)
                                                         : std::nullopt);
        if (workField)
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Work,
                                  hasWork && reportMm ? std::optional<QPointF>(work)
                                                      : std::nullopt);
        if (offsetField)
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Offset,
                                  knownOffset && reportMm ? std::optional<QPointF>(offset)
                                                          : std::nullopt);
        if (hasMachine && hasWork && knownOffset &&
            (std::abs(machine.x() - work.x() - offset.x()) > .002 ||
             std::abs(machine.y() - work.y() - offset.y()) > .002)) {
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Work, std::nullopt);
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Machine, std::nullopt);
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Offset, std::nullopt);
            fail("Widersprüchliche Maschinen-/Arbeitskoordinaten; Referenz verworfen.",
                 ControllerDiagnostics::FaultCause::CoordinateMismatch);
            return;
        }
        if (positionReferenced && hadOffset && knownOffset && previousOffset != offset &&
            !zeroPending && !homingPending) {
            diagnostic.coordinate(ControllerDiagnostics::Coordinate::Work, std::nullopt);
            fail("Arbeitskoordinaten unerwartet verändert; Referenz und Auftrag verworfen.",
                 ControllerDiagnostics::FaultCause::OffsetChanged);
            return;
        }
        if (hasWork) {
            position = work;
            knownPosition = true;
        } else if (hasMachine && knownOffset) {
            position = machine - offset;
            knownPosition = true;
        }
        diagnostic.coordinate(ControllerDiagnostics::Coordinate::Work,
                              knownPosition && reportMm ? std::optional<QPointF>(position)
                                                        : std::nullopt);
        if (knownPosition && knownOffset && knownOutput)
            lastStatus.restart();
        else
            lastStatus.invalidate();
        if (controllerState == "Alarm" || controllerState == "Door") {
            commissionedModalContract = commissionedHomingContract = false;
            positionReferenced = homingPending = false;
            connectionVerified = false;
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
            fail("Controller meldet Leistung im Leerlauf; Auftrag verworfen, Ausgang prüfen.",
                 ControllerDiagnostics::FaultCause::IdleOutputReported);
            return;
        }
        if (executing() && overrideFeedback && powerOverride > 100) {
            fail("Leistungs-Override über 100 %; Auftrag verworfen, Ausgang prüfen.",
                 ControllerDiagnostics::FaultCause::ExcessOverride);
            return;
        }
        if (waitingIdle && controllerState == "Idle" && knownPosition && knownOffset &&
            knownOutput && reportedOutput == 0 && !activeInputs) {
            waitingIdle = false;
            diagnostic.beginStream(false, diagnosticState());
            timeout.stop();
            progress = 1;
            if (homingPending) {
                positionReferenced = true;
                homingPending = false;
                append("Referenzfahrt abgeschlossen; Maschinenposition vom Controller bestätigt.");
            }
            firing = false;
            paused = pauseRequested = false;
            state = "Bereit";
            append("Controller meldet Idle: Auftrag abgeschlossen.");
        }
        if (!busy() && !handshaking) {
            if (!configurationValid())
                state = "GRBL-Konfiguration nicht freigegeben";
            else if (!knownPosition || !knownOffset || !knownOutput)
                state = "Position, Nullpunkt oder Leistung nicht bestätigt";
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
        if (ready())
            connectionVerified = true;
        emit changed();
        return;
    }
    if (s == "ok" && !waitingAck)
        diagnostic.unmatchedAcknowledgement();
    if (s == "ok" && waitingAck) {
        if (!diagnosticNormalizedAck) {
            if (buffering && inFlight.isEmpty())
                diagnostic.unmatchedAcknowledgement();
            else
                diagnostic.acknowledged();
        }
        if (buffering) {
            if (inFlight.isEmpty()) {
                fail("Unerwartete GRBL-Quittierung.",
                     ControllerDiagnostics::FaultCause::UnexpectedAck);
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
                        fail("Konfiguration nicht eindeutig lesbar; Verbindung geschlossen.",
                             ControllerDiagnostics::FaultCause::ConfigurationReadback);
                    return;
                }
                handshaking = false;
                state = configurationValid() ? "Warte auf Position"
                                             : "GRBL-Konfiguration nicht freigegeben";
                diagnostic.statusQuery(serial.write("?") == 1);
                poll.start();
            } else {
                if (zeroPending || homingPending) {
                    // Reports queued before G10 or $H completed may describe
                    // the old origin/position. Require fresh position and WCO.
                    knownPosition = knownOffset = false;
                    zeroPending = false;
                }
                waitingIdle = true;
                diagnostic.statusQuery(serial.write("?") == 1);
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
            fail("GCode-Zeile länger als GRBL-Zeilenpuffer.",
                 ControllerDiagnostics::FaultCause::InvalidReply);
            return;
        }
        if (buffering && rxBytes + bytes.size() > 100)
            break;
        if (serial.write(bytes) != bytes.size()) {
            fail("Serieller Schreibfehler.", ControllerDiagnostics::FaultCause::WriteFailure);
            return;
        }
        if (buffering) {
            inFlight.append(bytes.size());
            rxBytes += bytes.size();
            ++sent;
        } else
            pendingAcks = bytes.count('\n');
        auditDispatch(bytes);
        waitingAck = true;
        timeout.start();
    } while (buffering);
}
bool GrblSession::commands(const QStringList &lines, bool buffered) {
    if (!ready() || lines.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Dispatch,
                        ControllerDiagnostics::Denial::ControllerNotReady);
        append("Aktion abgelehnt: GRBL nicht im geprüften Leerlauf.");
        return false;
    }
    diagnostic.beginStream(buffered, diagnosticState());
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
    diagnostic.request(ControllerDiagnostics::Action::Start);
    if (const auto error = motionSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Start, diagnosticMotionDenial());
        append(error);
        return;
    }
    if (!positionReferenced) {
        append("Start gesperrt: Maschinenreferenz fehlt. Eine gemeldete Position allein genügt "
               "nicht.");
        return;
    }
    if (job.deviceProfile().outputFingerprint() != m_deviceProfile.outputFingerprint()) {
        diagnostic.deny(ControllerDiagnostics::Action::Start,
                        ControllerDiagnostics::Denial::ProfileMismatch);
        append("Geräteprofil des Auftrags passt nicht zur geprüften Verbindung.");
        return;
    }
    if (!insideMachineBed(position)) {
        diagnostic.deny(ControllerDiagnostics::Action::Start,
                        ControllerDiagnostics::Denial::PhysicalBounds);
        append("Aktuelle Maschinenposition oder Arbeitsnullpunkt nicht freigegeben.");
        return;
    }
    if (!job.valid()) {
        diagnostic.deny(ControllerDiagnostics::Action::Start,
                        ControllerDiagnostics::Denial::InvalidPlan);
        append(job.errors.join("; "));
        return;
    }
    // The encoder can insert a laser-off rapid to the first move.from. Check
    // both emitting endpoints and all targets, plus the actual current head
    // position. A rapid.from is a preview origin and is never emitted.
    for (const auto &move : job.moves)
        if ((!move.rapid && !insideMachineBed(move.from)) || !insideMachineBed(move.to)) {
            diagnostic.deny(ControllerDiagnostics::Action::Start,
                            ControllerDiagnostics::Denial::PhysicalBounds);
            append("Auftrag mit G54-Arbeitsnullpunkt außerhalb des Maschinenraums abgelehnt.");
            return;
        }
    const auto program = job.encodedProgram();
    if (!program.valid() || program.driverId() != m_deviceProfile.driverId() ||
        program.transport() != m_deviceProfile.transport() ||
        program.mediaType() != "text/x-gcode" ||
        program.profileFingerprint() != m_deviceProfile.outputFingerprint()) {
        diagnostic.deny(ControllerDiagnostics::Action::Start,
                        ControllerDiagnostics::Denial::OutputContract);
        append(program.errors().isEmpty() ? QString("Kein passender GRBL-Ausgabevertrag.")
                                          : program.errors().join("; "));
        return;
    }
    commands(program.gcodeLines(), true);
}
void GrblSession::pause() {
    if (!connected() || !busy() || firing || handshaking)
        return;
    paused = !paused;
    pauseRequested = paused;
    if (paused) {
        diagnostic.realtime(ControllerDiagnostics::Command::FeedHold, serial.write("!") == 1);
        timeout.stop();
        state = "Pausiert";
    } else {
        diagnostic.realtime(ControllerDiagnostics::Command::CycleResume, serial.write("~") == 1);
        state = "Läuft";
        if (waitingAck || waitingIdle)
            timeout.start(waitingIdle ? 30000 : 10000);
        pump();
    }
    emit changed();
}
void GrblSession::stop() {
    commissionedModalContract = commissionedHomingContract = false;
    positionReferenced = homingPending = false;
    if (!connected())
        return;
    focusTimer.stop();
    firing = false;
    paused = false;
    pauseRequested = zeroPending = false;
    knownOutput = false;
    queue.clear();
    inFlight.clear();
    sent = rxBytes = 0;
    buffering = false;
    waitingAck = waitingIdle = handshaking = false;
    awaitingReset = connectionVerified = false;
    timeout.stop();
    version = laserMode = powerScale = knownPosition = false;
    awaitingReset = true;
    state = "Abbruch · warte auf GRBL-Neustart";
    serial.clear(QSerialPort::Output);
    diagnostic.resetRequested(ControllerDiagnostics::ResetReason::Stop);
    diagnostic.resetWriteAccepted(serial.write("!\x18", 2) == 2);
    if (!connected())
        return;
    timeout.start(10000);
    append("Feed Hold und Soft-Reset gesendet; keine automatische Fortsetzung.");
}
void GrblSession::jog(double dx, double dy) {
    diagnostic.request(ControllerDiagnostics::Action::Jog);
    if (const auto error = motionSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Jog, diagnosticMotionDenial());
        append(error);
        return;
    }
    if (!positionReferenced) {
        append("Bewegung gesperrt: Maschinenreferenz fehlt.");
        return;
    }
    const auto target = position + QPointF(n(dx).toDouble(), n(dy).toDouble());
    if (!std::isfinite(dx) || !std::isfinite(dy) || !insideMachineBed(position) ||
        !insideMachineBed(target)) {
        diagnostic.deny(ControllerDiagnostics::Action::Jog,
                        ControllerDiagnostics::Denial::PhysicalBounds);
        append("Bewegung außerhalb des Arbeitsraums abgelehnt.");
        return;
    }
    commands({"M5", "$J=G91 G21 X" + n(dx) + " Y" + n(dy) + " F600"});
}
void GrblSession::home() {
    diagnostic.request(ControllerDiagnostics::Action::Home);
    if (const auto error = homingSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Home,
                        commissionedHomingContract
                            ? ControllerDiagnostics::Denial::HomingPrerequisite
                            : ControllerDiagnostics::Denial::HomingContract);
        append(error);
        return;
    }
    if (connected() && controllerState == "Alarm" && !busy() && version && laserMode &&
        powerScale && reportMm && bedWidth && bedHeight) {
        diagnostic.beginStream(false, diagnosticState());
        queue = {"$H"};
        total = 1;
        acked = 0;
        progress = 0;
        paused = false;
        positionReferenced = false;
        homingPending = true;
        state = "Referenzfahrt";
        pump();
        emit changed();
    } else if (ready()) {
        positionReferenced = false;
        homingPending = true;
        if (!commands({"M5", "$H"}))
            homingPending = false;
    }
}
void GrblSession::zero() {
    diagnostic.request(ControllerDiagnostics::Action::Zero);
    if (const auto error = motionSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Zero, diagnosticMotionDenial());
        append(error);
        return;
    }
    if (!positionReferenced) {
        append("Arbeitsnullpunkt nicht gesetzt: Maschinenreferenz fehlt.");
        return;
    }
    if (commands({"M5", "G10 L20 P1 X0 Y0", "G54"})) {
        zeroPending = true;
        knownPosition = false;
        knownOffset = false;
    }
}
void GrblSession::frame(const QVariantMap &b) {
    diagnostic.request(ControllerDiagnostics::Action::Frame);
    if (const auto error = motionSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Frame, diagnosticMotionDenial());
        append(error);
        return;
    }
    if (!positionReferenced) {
        append("Rahmenfahrt gesperrt: Maschinenreferenz fehlt.");
        return;
    }
    QRectF r(b.value("x").toDouble(), b.value("y").toDouble(), b.value("width").toDouble(),
             b.value("height").toDouble());
    if (!b.value("valid").toBool() || !Geometry::insideBed(r, m_deviceProfile.workArea())) {
        diagnostic.deny(ControllerDiagnostics::Action::Frame,
                        ControllerDiagnostics::Denial::InvalidRegion);
        append("Ungültige Rahmenfahrt.");
        return;
    }
    if (!insideMachineBed(position) || !insideMachineBed(r.topLeft()) ||
        !insideMachineBed(r.topRight()) || !insideMachineBed(r.bottomRight()) ||
        !insideMachineBed(r.bottomLeft())) {
        diagnostic.deny(ControllerDiagnostics::Action::Frame,
                        ControllerDiagnostics::Denial::PhysicalBounds);
        append("Rahmenfahrt mit G54-Arbeitsnullpunkt außerhalb des Maschinenraums abgelehnt.");
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
    diagnostic.request(ControllerDiagnostics::Action::Focus);
    if (const auto error = motionSafetyError(); !error.isEmpty()) {
        diagnostic.deny(ControllerDiagnostics::Action::Focus, diagnosticMotionDenial());
        append(error);
        return;
    }
    if (!ready()) {
        diagnostic.deny(ControllerDiagnostics::Action::Focus,
                        ControllerDiagnostics::Denial::ControllerNotReady);
        return;
    }
    // A stationary GRBL M3 output has no independent watchdog. Host deadline and
    // release reset it; communication loss cannot guarantee output shutdown.
    if (!insideMachineBed(position))
        return;
    // Send the finite pulse and its shutdown together, within the 128-byte RX buffer.
    const auto settings = m_deviceProfile.driverSettings();
    const auto output =
        qRound64(settings["focusPower"].toDouble() * settings["powerScale"].toDouble() / 100.);
    if (!commands({"M5\nG21\nG90\nG1 F1 M3 S" + QString::number(output) + "\nG4 P0.2\nM5 S0\nG0"}))
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
    diagnostic.realtime(ControllerDiagnostics::Command::Override,
                        serial.write(bytes) == bytes.size());
    diagnostic.statusQuery(serial.write("?") == 1);
    return true;
}
