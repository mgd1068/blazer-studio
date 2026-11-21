#include "machine.h"
#include "document.h"
#include <QPointer>
Machine::Machine(Document *d, QObject *p, HardwareAccess access)
    : Simulator(p), document(d), hardwareAccess(access) {
    accessoryClock.start();
    accessoryWatchdog.setInterval(50);
    connect(&accessoryWatchdog, &QTimer::timeout, this, [this] {
        if (live && (accessoryJobActive || firing()) && !processingSafetyErrors().isEmpty()) {
            document->notify("Auftrag abgebrochen: " + processingSafetyErrors().join("\n"));
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
}
void Machine::syncDeviceProfile() {
    if (session && session->deviceProfile().outputFingerprint() ==
                       document->deviceProfile().outputFingerprint())
        return;
    live = false;
    accessory = AccessoryContract(true);
    accessoryJobActive = false;
    QString error;
    session = ControllerSessions::create(document->deviceProfile(), error);
    if (session)
        connect(session.get(), &ControllerSession::changed, this, [this] {
            if (live && accessoryJobActive && !session->status().executing) {
                accessoryJobActive = false;
                accessory.transition(session->status().connected ? AccessoryContract::Event::Complete
                                                                : AccessoryContract::Event::Disconnect);
            }
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
    live = true;
    bool result = session->open(port);
    emit changed();
    return result;
}
void Machine::useSimulator() {
    accessory.transition(AccessoryContract::Event::Disconnect);
    accessoryJobActive = false;
    if (session)
        session->close();
    live = false;
    emit changed();
}
void Machine::toggleConnection() {
    if (live)
        session->close();
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
    if (on && live && !processingSafetyErrors().isEmpty()) {
        document->notify(processingSafetyErrors().join("\n"));
        return;
    }
    if (live)
        session->focus(on);
    else
        Simulator::focus(on);
}
void Machine::frame(const QVariantMap &b) {
    if (b.value("outputFingerprint").toString() !=
        QString::fromLatin1(document->deviceProfile().outputFingerprint())) {
        document->notify("Rahmenziel gehört zu einem anderen Geräteprofil.");
        return;
    }
    if (live)
        session->frame(b);
    else
        Simulator::frame(b);
}
void Machine::start() {
    if (document->planningBusy()) {
        document->notify("Auftragsberechnung oder Export läuft noch.");
        return;
    }
    if (busy() || firing())
        return;
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
    if (live)
        session->stop();
    else
        Simulator::stop();
}
void Machine::disconnectFault() {
    accessory.transition(AccessoryContract::Event::Disconnect);
    accessoryJobActive = false;
    if (live)
        session->close();
    else
        Simulator::disconnectFault();
}

QVariantList Machine::capabilities() const {
    return {
        QVariantMap{{"name", "XY / GRBL / Referenzfahrt"},
                    {"status", "Vorhanden"},
                    {"detail", "M3: Grundkonfiguration und erste Kraftpapierbearbeitung bestätigt; "
                               "vollständige Geräteabnahme offen."}},
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
                    {"status", "Protokoll ungeklärt"},
                    {"detail", "Keine geratenen Lüfterbefehle; manuelle Bedienung erforderlich."}}};
}
