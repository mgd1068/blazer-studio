#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include <QFile>
#include <QScopedValueRollback>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

DeviceManager::DeviceManager(Document *d, Machine *m, QString path, QObject *parent)
    : QObject(parent), document(d), machine(m), library(std::move(path)) {
    connect(&library, &DeviceLibrary::changed, this, &DeviceManager::changed);
    connect(machine, &Simulator::changed, this, &DeviceManager::changed);
    connect(document, &Document::changed, this, &DeviceManager::changed);
    connect(this, &DeviceManager::changed, this, [this] {
        machine->setPreviewDeviceBinding(bindingKey().toUtf8());
    });
    connect(document, &Document::deviceProfileChanged, this, [this] {
        if (activating || selectedId.isEmpty()) return;
        const auto selected = library.profile(selectedId);
        if (!selected || selected->outputFingerprint() != document->deviceProfile().outputFingerprint())
            invalidateSelection();
    });
    if (!library.writable()) lastMessage = library.message();
}
bool DeviceManager::fail(QString text) {
    lastMessage = std::move(text);
    emit changed();
    return false;
}
QString DeviceManager::changeBlockReason() const {
    if (changing) return "Geräteänderung läuft. Bitte warten.";
    if (!library.writable()) return library.message();
    if (!machine->simulation() && machine->connected())
        return "Gerätezuordnung gesperrt: zuerst die echte Verbindung trennen.";
    if (machine->busy() || machine->firing())
        return "Gerätezuordnung gesperrt: laufenden Auftrag oder Fokus beenden.";
    if (document->planningBusy() || document->batchBusy() || document->imageExportBusy())
        return "Gerätezuordnung gesperrt: Berechnung oder Export abwarten.";
    return {};
}
bool DeviceManager::allowChange() {
    if (changing) return false; // No recursive notifications from an in-flight mutation.
    const auto reason = changeBlockReason();
    return reason.isEmpty() || fail(reason);
}
void DeviceManager::refreshEndpoints() {
    if (changing) return;
    discoveredEndpoints = machine->ports(); // Metadata only; no endpoint is opened.
    emit changed();
}
void DeviceManager::invalidateSelection() {
    selectedId.clear(); selectedBinding.clear();
    lastMessage = "Gerätezuordnung hat sich geändert. Das physische Gerät erneut auswählen; keine Verbindung wurde geöffnet.";
    emit changed();
}
QString DeviceManager::bindingKey() const {
    if (!library.writable()) return {};
    if (selectedId.isEmpty() || library.bindingKey(selectedId) != selectedBinding) return {};
    const auto selected = library.profile(selectedId);
    if (!selected || selected->outputFingerprint() != document->deviceProfile().outputFingerprint()) return {};
    return selectedBinding;
}
QVariantMap DeviceManager::active() const {
    return bindingKey().isEmpty() ? QVariantMap{} : library.instance(selectedId);
}
QString DeviceManager::parameterHash() const {
    if (bindingKey().isEmpty()) return {};
    // Processing parameters, not geometric layout or portable physical grants.
    // A material history match never confirms the current material checkbox.
    QSet<QString> signatures;
    for (const auto &art : document->items()) {
        if (!art.visible || art.toolLayer || art.page != document->activePage()) continue;
        QVariantList extraOperations;
        for (const auto operation : art.extraOperations) extraOperations.append(operation);
        const QVariantMap values{{"kind", art.kind}, {"layer", art.layer},
                                 {"extraOperations", extraOperations},
                                 {"outline", art.engraveOutline}, {"settings", art.settings},
                                 {"style", art.style}, {"relief", art.relief.toMap()}};
        signatures.insert(QString::fromUtf8(QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact)));
    }
    auto sorted = signatures.values(); std::sort(sorted.begin(), sorted.end());
    const QVariantMap parameters{{"contract", 1},
                                {"profile", QString::fromLatin1(document->deviceProfile().outputFingerprint())},
                                {"material", document->material()}, {"layers", document->layers()},
                                {"operations", sorted}};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument::fromVariant(parameters).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QString DeviceManager::addCurrent(QString name, QString endpoint) {
    if (!allowChange()) return {};
    Mutation mutation(*this);
    const auto id = library.add(std::move(name), document->deviceProfile(), std::move(endpoint));
    if (id.isEmpty()) { fail(library.message()); return {}; }
    lastMessage = "Gerät angelegt. Zum Verwenden ausdrücklich auswählen; es wurde nichts verbunden.";
    emit changed(); return id;
}
QString DeviceManager::importProfile(const QUrl &url, QString name, QString endpoint) {
    if (!allowChange()) return {};
    Mutation mutation(*this);
    if (!url.isLocalFile()) { fail("Eine lokale Geräteprofildatei auswählen."); return {}; }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        fail("Geräteprofil nicht lesbar oder größer als 64 KiB."); return {};
    }
    const auto bytes = file.read(65537);
    if (bytes.size() > 65536 || !file.atEnd() || file.error() != QFileDevice::NoError) { fail("Geräteprofil konnte nicht vollständig innerhalb des Größenlimits gelesen werden."); return {}; }
    QString error;
    const auto profile = DeviceProfile::parse(bytes, error);
    if (!profile) { fail(error); return {}; }
    const auto id = library.add(std::move(name), *profile, std::move(endpoint));
    if (id.isEmpty()) { fail(library.message()); return {}; }
    lastMessage = "Profil importiert. Profilbeschreibung und Softwareunterstützung sind keine Geräteabnahme.";
    emit changed(); return id;
}
bool DeviceManager::activate(const QString &id) {
    if (!allowChange()) return false;
    Mutation mutation(*this);
    const auto profile = library.profile(id);
    if (!profile) return fail("Gerät nicht gefunden.");
    QScopedValueRollback<bool> guard(activating, true);
    if (!document->setDeviceProfile(*profile)) return fail(document->message());
    selectedId = id; selectedBinding = library.bindingKey(id);
    lastMessage = "Gerät ausgewählt. Material und Kalibrierung für dieses einzelne Gerät prüfen. Keine Verbindung geöffnet.";
    emit changed(); return true;
}
bool DeviceManager::update(const QString &id, QString name, QString endpoint) {
    if (!allowChange()) return false;
    Mutation mutation(*this);
    const auto profile = library.profile(id);
    if (!profile) return fail("Gerät nicht gefunden.");
    if (!library.update(id, std::move(name), *profile, std::move(endpoint))) return fail(library.message());
    if (id == selectedId && library.bindingKey(id) != selectedBinding) invalidateSelection();
    else { lastMessage = "Gerät gespeichert."; emit changed(); }
    return true;
}
QString DeviceManager::duplicate(const QString &id, QString name) {
    if (!allowChange()) return {};
    Mutation mutation(*this);
    const auto copy = library.duplicate(id, std::move(name));
    if (copy.isEmpty()) { fail(library.message()); return {}; }
    lastMessage = "Eigenständiges Gerät kopiert. Anschluss und Gerätefreigaben wurden nicht übernommen.";
    emit changed(); return copy;
}
bool DeviceManager::remove(const QString &id) {
    if (!allowChange()) return false;
    Mutation mutation(*this);
    if (!library.remove(id)) return fail(library.message());
    if (id == selectedId) invalidateSelection();
    else { lastMessage = "Gerät entfernt. Projektdateien bleiben erhalten."; emit changed(); }
    return true;
}
bool DeviceManager::connectSelected() {
    if (changing) return false;
    if (!library.writable()) return fail(library.message());
    if (bindingKey().isEmpty()) return fail("Zuerst ein physisches Gerät in der Geräteverwaltung auswählen.");
    if (!machine->simulation() && machine->connected()) return fail("Eine echte Verbindung ist bereits geöffnet; zum Wechseln zuerst trennen.");
    if (machine->busy() || machine->firing() || document->planningBusy() || document->batchBusy() || document->imageExportBusy()) return fail("Verbindung während Auftrag, Fokus, Berechnung oder Export gesperrt.");
    Mutation mutation(*this);
    const auto endpoint = active()["endpoint"].toString();
    if (endpoint.isEmpty()) return fail("Für dieses Gerät fehlt der lokale Anschluss. In der Geräteverwaltung eintragen.");
    const auto error = DeviceBackends::runtimeSupportError(*library.profile(selectedId));
    if (!error.isEmpty()) return fail(error);
    if (!machine->connectHardware(endpoint)) return fail(document->message());
    lastMessage = "Verbindungsprüfung gestartet. Maschinenreferenz, Koordinaten und Absaugung benötigen eigene Nachweise.";
    emit changed(); return true;
}
