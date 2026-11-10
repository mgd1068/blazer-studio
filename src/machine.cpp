#include "machine.h"
#include "document.h"
#include <QSerialPortInfo>
Machine::Machine(Document *d, QObject *p) : Simulator(p), document(d) {
    connect(&grbl, &GrblSession::changed, this, &Simulator::changed);
}
QStringList Machine::ports() const {
    QStringList usb, other;
    for (const auto &p : QSerialPortInfo::availablePorts())
        (p.hasVendorIdentifier() ? usb : other).append(p.systemLocation());
    usb.sort();
    other.sort();
    return usb + other;
}
bool Machine::connectHardware(const QString &port) {
    if (busy() || firing())
        return false;
    if (grbl.connected())
        grbl.close();
    Simulator::stop();
    live = true;
    bool result = grbl.open(port);
    emit changed();
    return result;
}
void Machine::useSimulator() {
    grbl.close();
    live = false;
    emit changed();
}
void Machine::toggleConnection() {
    if (live)
        grbl.close();
    else
        Simulator::toggleConnection();
}
void Machine::jog(double x, double y) {
    if (live)
        grbl.jog(x, y);
    else
        Simulator::jog(x, y);
}
void Machine::home() {
    if (live)
        grbl.home();
    else
        Simulator::home();
}
void Machine::focus(bool on) {
    if (live)
        grbl.focus(on);
    else
        Simulator::focus(on);
}
void Machine::frame(const QVariantMap &b) {
    if (live)
        grbl.frame(b);
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
    const auto &job = document->job();
    if (!job.valid()) {
        document->notify(job.errors.join("; "));
        return;
    }
    if (live)
        grbl.start(job);
    else {
        Simulator::setJob(job);
        Simulator::start();
    }
}
void Machine::pause() {
    if (live)
        grbl.pause();
    else
        Simulator::pause();
}
void Machine::stop() {
    if (live)
        grbl.stop();
    else
        Simulator::stop();
}
void Machine::disconnectFault() {
    if (live)
        grbl.close();
    else
        Simulator::disconnectFault();
}

QVariantList Machine::capabilities() const {
    return {
        QVariantMap{{"name", "XY / GRBL / Referenzfahrt"},
                    {"status", "Vorhanden"},
                    {"detail", "M3: Konfiguration und Bewegung ohne Laser bestätigt."}},
        QVariantMap{{"name", "Kamera und Registrierung"},
                    {"status", "Keine Kamera eingerichtet"},
                    {"detail", "Kein kalibriertes Kamerabild vorhanden; keine automatische "
                               "Positionierung freigegeben."}},
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
