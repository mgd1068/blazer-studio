#include "machine.h"
#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "nativeinputintent.h"
#include "document.h"
#include <QPointer>
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <QUuid>
Machine::Machine(Document *d, QObject *p, HardwareAccess access)
    : Simulator(p), document(d), hardwareAccess(access) {
    nativeInput = new NativeInputIntentAdapter(this);
    connect(nativeInput,&NativeInputIntentAdapter::invalidated,this,[this] {
        if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()))
            runtime->revokeFocusContext();
    });
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    document->addDeviceProfileChangeGuard([self = QPointer<Machine>(this)] {
        return !self || (!self->occupied() && !self->requestPending() && !self->busy() && !self->firing() &&
                         (!self->session || !self->session->status().connected));
    });
    document->addSynchronousWorkGuard([self = QPointer<Machine>(this)]() -> QString {
        if (!self || self->simulation()) return {};
        if (self->occupied() || self->requestPending() || self->busy() || self->firing() || self->session->status().accessoryLeaseActive ||
            self->session->status().accessoryJobActive || self->session->status().accessoryFocusActive)
            return "Schwere Datei- und Geometrieaktionen warten, bis die aktive Geräteaktion beendet ist. STOPP bleibt verfügbar; die automatische Sicherung wird danach nachgeholt.";
        return {};
    });
    connect(this, &Simulator::changed, document, &Document::notifySynchronousWorkGuardChanged);
    connect(this, &QObject::destroyed, document, &Document::notifySynchronousWorkGuardChanged);
    document->notifySynchronousWorkGuardChanged();
    syncDeviceProfile();
    connect(d, &Document::deviceProfileChanged, this, &Machine::syncDeviceProfile);
    connect(this, &Simulator::changed, this, &Machine::refreshPreviewPosition);
    connect(d, &Document::changed, this, &Machine::refreshPreviewPosition);
    connect(d, &Document::changed, this, [this] {
        if (live && !document->planningBusy())
            if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get())) runtime->prepare(document->job());
    });
    refreshPreviewPosition();
}
void Machine::setPreviewDeviceBinding(const QByteArray &binding) {
    if (previewDeviceBinding == binding) return;
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    previewDeviceBinding = binding; // Publish before STOPP notifications can reenter the manager.
    if (live && (connected() || occupied() || requestPending())) {
        document->notify("Gerätebindung geändert. Verbindung vor weiterer Bearbeitung erneut prüfen.");
        session->close();
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
    if (hardwareAccess == HardwareAccess::Disabled) {
        // Offline export/self-test never needs a transport owner or its thread.
        live = false;
        pendingSimulator = false;
        session.reset();
        const QPointer<Machine> alive(this);
        Simulator::setDeviceProfile(document->deviceProfile());
        if (alive) emit changed();
        return;
    }
    if (session && session->deviceProfile().outputFingerprint() ==
                       document->deviceProfile().outputFingerprint())
        return;
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    live = false;
    QString error;
    session = ControllerSessions::create(document->deviceProfile(), error);
    if (session)
        connect(session.get(), &ControllerSession::changed, this, [this] {
            const QPointer<Machine> alive(this);
            const auto status=session->status();
            const bool revokeInput=status.connectionEpoch!=nativeConnectionEpoch ||
                                   (nativeSessionVerified && !status.verified);
            nativeConnectionEpoch=status.connectionEpoch;
            nativeSessionVerified=status.verified;
            if (revokeInput) {
                nativeInput->revoke();
                if (!alive) return;
            }
            if (pendingSimulator && !session->status().connected && !session->status().occupied) {
                pendingSimulator = false;
                live = false;
                simulatorFrameNonce = QUuid::createUuid().toRfc4122();
                Simulator::stop();
                if (!alive) return;
                refreshPreviewPosition();
                if (!alive) return;
            }
            emit changed();
        });
    else
        document->notify(error);
    if (session)
        connect(session.get(), &ControllerSession::connectionRequestFinished,
                this, &Machine::connectionRequestFinished);
    Simulator::setDeviceProfile(document->deviceProfile());
    emit changed();
}
QStringList Machine::ports() const {
    if (hardwareAccess == HardwareAccess::Disabled)
        return {};
    return ControllerSessions::availableEndpoints(document->deviceProfile());
}
bool Machine::connectHardware(const QString &port) {
    return requestConnectHardware(port, {}).enqueued;
}
ControllerSubmission Machine::requestConnectHardware(const QString &port, const QByteArray &binding) {
    if (hardwareAccess == HardwareAccess::Disabled)
        return {0,false,"Hardwareanschluss in diesem Offline-Lauf deaktiviert."};
    const auto error = DeviceBackends::runtimeSupportError(document->deviceProfile());
    if (!error.isEmpty()) return {0,false,error};
    if (!session || occupied() || session->status().connected)
        return {0,false,"Verbindung oder Geräteaktion noch belegt."};
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    Simulator::stop();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    live = true; pendingSimulator = false;
    document->clearPreviewPosition();
    if (!alive) return {0,false,"Verbindungsbesitzer beendet."};
    const auto result=session->requestOpen(port,binding);
    if (!alive) return result;
    if (auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); runtime && !document->planningBusy()) runtime->prepare(document->job());
    if (!alive) return result;
    if (!result.enqueued) document->notify(result.reason);
    if (!alive) return result;
    emit changed(); return result;
}
bool Machine::occupied() const { return pendingSimulator || busy() || firing() || (live && session && session->status().occupied); }
bool Machine::requestPending() const { return pendingSimulator || (live && session && session->status().requestPending); }
bool Machine::statusFresh() const { return live && session && session->status().statusFresh; }
qint64 Machine::statusAgeMs() const { return live && session ? session->status().statusAgeMs : -1; }
QString Machine::connectionPhase() const { return pendingSimulator?QString("closing"):live && session ? session->status().connectionPhase : "closed"; }
bool Machine::jobPreparing() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return live && runtime && runtime->jobPreparing(); }
bool Machine::jobPrepared() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return !live || (runtime&&runtime->jobPrepared()); }
QString Machine::prepareReason() const { const auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get()); return live&&runtime?runtime->prepareReason():QString(); }

QString Machine::nativeInputSafetyError() const { return live ? nativeInput->reason() : QString(); }
void Machine::registerFocusControl(QObject *control) {
    focusControl = control;
    nativeInput->registerControl(control,"focus");
}
void Machine::beginFocus() {
    if (!live) { Simulator::focus(true); return; }
    auto runtime=dynamic_cast<ControllerRuntimeSession *>(session.get());
    const auto input=nativeInput->consume("focus");
    if (!runtime || !input) {
        const auto reason=nativeInput->reason();
        document->notify(reason.isEmpty() ? QString("Fokus gesperrt: frischer nativer Druck auf das Fokusfeld fehlt.") : reason);
        return;
    }
    const auto stamp=runtime->sealNativePress(input->interaction,
        runtime->m_mailbox->focusWindowEpoch.load(),input->observedAtMs);
    runtime->beginFocus(stamp);
}
void Machine::endFocus() { focus(false); }
void Machine::useSimulator() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live && session && (session->status().connected || session->status().occupied)) {
        pendingSimulator = true; session->close(); if (!alive) return; emit changed(); return;
    }
    pendingSimulator = false; live = false;
    simulatorFrameNonce = QUuid::createUuid().toRfc4122();
    Simulator::stop(); if (!alive) return; refreshPreviewPosition(); if (!alive) return; emit changed();
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
void Machine::setWorkZero() {
    if (!live || !session) return;
    if (busy() || firing() || session->status().accessoryLeaseActive || session->status().accessoryJobActive || session->status().accessoryFocusActive) {
        document->notify("Arbeitsnullpunkt gesperrt: erst den aktiven Auftrag, Fokus oder die laufende Geräteaktion beenden.");
        return;
    }
    session->zero();
}
void Machine::home() {
    if (live)
        session->home();
    else
        Simulator::home();
}
void Machine::focus(bool on) {
    if (!live) { Simulator::focus(on); return; }
    if (!on) session->focus(false);
    else beginFocus(); // Legacy true has no input authority and cannot bypass the native gate.
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
        session->start(job);
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
        session->pause();
    } else
        Simulator::pause();
}
void Machine::stop() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
    if (live)
        session->stop();
    else
        Simulator::stop();
}
void Machine::disconnectFault() {
    const QPointer<Machine> alive(this);
    nativeInput->revoke();
    if (!alive) return;
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
