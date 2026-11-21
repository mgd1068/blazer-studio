#include "machine.h"
#include "document.h"
#include <QPointer>
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <QUuid>
Machine::Machine(Document *d, QObject *p, HardwareAccess access)
    : Simulator(p), document(d), hardwareAccess(access) {
    accessoryClock.start();
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    accessoryWatchdog.setInterval(50);
    connect(&accessoryWatchdog, &QTimer::timeout, this, [this] {
        const auto errors = processingSafetyErrors();
        if (errors != lastProcessingSafetyErrors) {
            lastProcessingSafetyErrors = errors;
            emit changed(); // Also refresh stale airflow/status evidence while idle.
        }
        const auto status = session ? session->status() : ControllerStatus{};
        const bool unsafeMotion = live && status.executing && !status.referencing && !status.resetting &&
            (!status.verified || !status.positionReferenced || !status.motionSafetyError.isEmpty());
        const bool unsafeOutput = live && (accessoryJobActive || accessoryFocusActive || firing()) &&
            !errors.isEmpty();
        if (unsafeMotion || unsafeOutput) {
            document->notify("Auftrag abgebrochen: " + errors.join("\n"));
            stop();
        }
    });
    accessoryWatchdog.start();
    document->addDeviceProfileChangeGuard([self = QPointer<Machine>(this)] {
        return !self || (!self->busy() && !self->firing() &&
                         (!self->session || !self->session->status().connected));
    });
    syncDeviceProfile();
    connect(d, &Document::deviceProfileChanged, this, &Machine::syncDeviceProfile);
    connect(this, &Simulator::changed, this, &Machine::refreshPreviewPosition);
    connect(d, &Document::changed, this, &Machine::refreshPreviewPosition);
    refreshPreviewPosition();
}
void Machine::setPreviewDeviceBinding(const QByteArray &binding) {
    if (previewDeviceBinding == binding) return;
    previewDeviceBinding = binding; // Publish before STOPP notifications can reenter the manager.
    if (live && connected()) {
        document->notify("Gerätebindung geändert. Verbindung vor weiterer Bearbeitung erneut prüfen.");
        stop();
    }
    refreshPreviewPosition();
}
void Machine::refreshPreviewPosition() {
    if (updatingPreview) return;
    QScopedValueRollback<bool> guard(updatingPreview, true);
    if (live) {
        document->clearPreviewPosition(); // Never reuse simulated coordinates for hardware.
        return;
    }
    // Freeze the job's start snapshot during simulation or a focus indication.
    if (busy() || firing()) return;
    if (!Simulator::ready()) { document->clearPreviewPosition(); return; }
    const auto profile = document->deviceProfile().outputFingerprint();
    const auto frame = QCryptographicHash::hash(
        QByteArray("blazer-simulator-frame-v1:") + simulatorFrameNonce + profile + previewDeviceBinding,
        QCryptographicHash::Sha256).toHex();
    const QPointF point(Simulator::x(),Simulator::y());
    if (previewPositionRevision == 0 || frame != previewFrame ||
        point.x() != previewPoint.x() || point.y() != previewPoint.y()) {
        ++previewPositionRevision;
        previewFrame = frame; previewPoint = point;
    }
    document->setPreviewPosition({point,true,profile,frame,previewPositionRevision});
}
void Machine::syncDeviceProfile() {
    if (session && session->deviceProfile().outputFingerprint() ==
                       document->deviceProfile().outputFingerprint())
        return;
    live = false;
    accessory = AccessoryContract(true);
    accessoryJobActive = false;
    accessoryFocusActive = false;
    sessionWasVerified = sessionWasReferenced = false;
    QString error;
    session = ControllerSessions::create(document->deviceProfile(), error);
    if (session)
        connect(session.get(), &ControllerSession::changed, this, [this] {
            const auto status = session->status();
            if (live && !status.connected) {
                accessory.transition(AccessoryContract::Event::Disconnect);
                accessoryJobActive = false;
                accessoryFocusActive = false;
            } else if (live && ((sessionWasVerified && !status.verified) ||
                                (sessionWasReferenced && !status.positionReferenced))) {
                // Preserve the loss boundary before a protective STOPP emits
                // nested session notifications. Never erase the active-output
                // evidence and then rely on a watchdog to rediscover it.
                const bool mustStop = (status.executing && !status.referencing && !status.resetting) || status.firing ||
                    accessoryJobActive || accessoryFocusActive;
                sessionWasVerified = status.verified;
                sessionWasReferenced = status.positionReferenced;
                accessory.transition(AccessoryContract::Event::Fault);
                if (status.resetting && !status.firing) {
                    // The adapter has already requested a host reset. Fault the
                    // lease without sending a second reset from its callback.
                    // A mere handshake is not this host-reset boundary.
                    accessoryJobActive = false;
                    accessoryFocusActive = false;
                } else if (mustStop) {
                    document->notify("Auftrag abgebrochen: Positions- oder Verbindungsnachweis verloren.");
                    stop();
                    return;
                }
            } else if (live && (accessoryJobActive || accessoryFocusActive) && !status.executing && !status.firing) {
                accessoryJobActive = false;
                accessoryFocusActive = false;
                accessory.transition(AccessoryContract::Event::Complete);
            }
            sessionWasVerified = status.verified;
            sessionWasReferenced = status.positionReferenced;
            emit changed();
        });
    else
        document->notify(error);
    Simulator::setDeviceProfile(document->deviceProfile());
    emit changed();
}
QStringList Machine::ports() const {
    if (hardwareAccess == HardwareAccess::Disabled)
        return {};
    return ControllerSessions::availableEndpoints(document->deviceProfile());
}
bool Machine::connectHardware(const QString &port) {
    if (hardwareAccess == HardwareAccess::Disabled) {
        document->notify("Hardwareanschluss in diesem Offline-Lauf deaktiviert.");
        return false;
    }
    const auto profileError = DeviceBackends::runtimeSupportError(document->deviceProfile());
    if (!profileError.isEmpty()) {
        document->notify(profileError);
        return false;
    }
    if (!session) {
        document->notify("Kein Verbindungsadapter für dieses Geräteprofil verfügbar.");
        return false;
    }
    if (busy() || firing())
        return false;
    if (session->status().connected)
        session->close();
    Simulator::stop();
    accessory = AccessoryContract(true);
    accessoryJobActive = false;
    accessoryFocusActive = false;
    live = true;
    document->clearPreviewPosition();
    bool result = session->open(port);
    emit changed();
    return result;
}
void Machine::useSimulator() {
    accessory.transition(AccessoryContract::Event::Disconnect);
    accessoryJobActive = false;
    accessoryFocusActive = false;
    if (session)
        session->close();
    live = false;
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    emit changed();
}
void Machine::toggleConnection() {
    if (live)
        disconnectFault();
    else
        Simulator::toggleConnection();
}
void Machine::jog(double x, double y) {
    if (live)
        session->jog(x, y);
    else
        Simulator::jog(x, y);
}
void Machine::home() {
    if (live)
        session->home();
    else
        Simulator::home();
}
void Machine::focus(bool on) {
    if (on && live && (busy() || firing())) return;
    if (on && live && !processingSafetyErrors().isEmpty()) {
        document->notify(processingSafetyErrors().join("\n"));
        return;
    }
    if (live) {
        if (on && !accessory.begin(accessoryClock.elapsed())) {
            document->notify(extractionSafetyError()); return;
        }
        if (on) accessoryFocusActive = true;
        session->focus(on);
        accessoryFocusActive = session->status().firing;
        if (!accessoryFocusActive && !accessoryJobActive) accessory.transition(AccessoryContract::Event::Complete);
    } else
        Simulator::focus(on);
}
void Machine::frame(const QVariantMap &b) {
    if (b.value("outputFingerprint").toString() !=
        QString::fromLatin1(document->deviceProfile().outputFingerprint())) {
        document->notify("Rahmenziel gehört zu einem anderen Geräteprofil.");
        return;
    }
    if (live && document->startMode() != Document::AbsoluteCoordinates) {
        document->notify("Relative Rahmenfahrt ist für reale Hardware noch nicht eingerichtet.");
        return;
    }
    const auto current = document->framingBounds(b.value("scope").toString());
    if (!b.value("valid").toBool() || !current.value("valid").toBool() || b != current) {
        document->notify("Rahmenziel ist veraltet oder verändert. Aktuelle Vorschau erneut prüfen.");
        return;
    }
    if (live)
        session->frame(current);
    else
        Simulator::frame(current);
}
void Machine::start() {
    if (document->planningBusy()) {
        document->notify("Auftragsberechnung oder Export läuft noch.");
        return;
    }
    if (busy() || firing())
        return;
    if (live && document->startMode() != Document::AbsoluteCoordinates) {
        document->notify("Relative Startmodi sind derzeit nur im Simulator eingerichtet. Reale Positions- und Koordinatenbindung ist noch nicht abgenommen.");
        return;
    }
    if (live && !processingSafetyErrors().isEmpty()) {
        document->notify(processingSafetyErrors().join("\n"));
        return;
    }
    const auto &job = document->job();
    if (job.deviceProfile().outputFingerprint() != document->deviceProfile().outputFingerprint()) {
        document->notify("Geräteprofil des Auftrags ist veraltet; bitte neu berechnen.");
        return;
    }
    if (!job.valid()) {
        document->notify(job.errors.join("; "));
        return;
    }
    if (live) {
        if (!accessory.begin(accessoryClock.elapsed())) {
            document->notify(extractionSafetyError()); return;
        }
        session->start(job);
        accessoryJobActive = session->status().executing;
        if (!accessoryJobActive) accessory.transition(AccessoryContract::Event::Complete);
    } else {
        Simulator::setJob(job);
        Simulator::start();
    }
}
void Machine::pause() {
    if (live) {
        if (paused() && !processingSafetyErrors().isEmpty()) {
            document->notify(processingSafetyErrors().join("\n"));
            stop(); return;
        }
        accessory.transition(AccessoryContract::Event::Pause);
        session->pause();
    } else
        Simulator::pause();
}
void Machine::stop() {
    accessory.transition(AccessoryContract::Event::Stop);
    if (live) accessory.transition(AccessoryContract::Event::Fault); // GRBL stop uses reset: physical output becomes unknown.
    accessoryJobActive = false;
    accessoryFocusActive = false;
    if (live)
        session->stop();
    else
        Simulator::stop();
}
void Machine::disconnectFault() {
    accessory.transition(AccessoryContract::Event::Disconnect);
    accessoryJobActive = false;
    accessoryFocusActive = false;
    if (live)
        session->close();
    else
        Simulator::disconnectFault();
}

QVariantList Machine::capabilities() const {
    return {
        QVariantMap{{"name", "XY / GRBL / Referenzfahrt"},
                    {"status", "Protokoll vorhanden · Gerätebetrieb gesperrt"},
                    {"detail", "Nach gemeldeter Kollision sind Maschinenkoordinaten, Grenzen und "
                               "Nullpunktzuordnung nicht unabhängig bestätigt. Keine Hardwarefreigabe."}},
        QVariantMap{{"name", "Kamera und Registrierung"},
                    {"status", "Referenzfoto, keine Live-Anbindung"},
                    {"detail", "Manuelles Bild mit Vierpunktregistrierung verfügbar; Live-Kamera "
                               "und automatische Positionierung nicht abgenommen."}},
        QVariantMap{{"name", "Rotary / Z / Autofokus"},
                    {"status", "Nicht nachgewiesen"},
                    {"detail", "Geräteprofil verwendet nur X/Y. Keine ungeprüften Achsenbefehle."}},
        QVariantMap{{"name", "Oberflächenvermessung / Fokusnachführung"},
                    {"status", "Keine Messhardware"},
                    {"detail", "Keine automatische Höhen- oder Fokuskompensation."}},
        QVariantMap{{"name", "Absaugung separat schalten"},
                    {"status", "Ausgangszuordnung und Luftstrom nicht bestätigt"},
                    {"detail", "Gehäuseabsaugung und Air Assist sind getrennt. Unbekannter "
                               "Zubehörzustand gibt keine Bearbeitung frei."}}};
}

QVariantMap Machine::controllerDiagnosticSnapshot() const {
    // Numeric protocol evidence is available only through the explicit preview
    // workflow. Never collect raw logs, ports, document text or material notes.
    return {{"schema", 1},
            {"controller", live && session ? session->diagnosticSnapshot()
                : QVariantMap{{"supported",false},{"reasonCode","simulator"}}},
            {"facade", QVariantMap{
                {"simulation",simulation()}, {"connected",connected()}, {"ready",ready()},
                {"positionReferenced",positionReferenced()},
                {"motionContractBlocked",!motionSafetyError().isEmpty()},
                {"homingContractBlocked",!homingSafetyError().isEmpty()},
                {"extractionContractBlocked",!extractionSafetyError().isEmpty()},
                {"relativeHardwareStartBlocked",live && document->startMode()!=Document::AbsoluteCoordinates}}}};
}
