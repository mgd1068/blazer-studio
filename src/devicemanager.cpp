#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include <QFile>
#include <QScopedValueRollback>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSettings>
#include <QFileInfo>
#include <QTemporaryDir>
#include <optional>
#include <algorithm>

namespace {
QString autoHomeKey(const QString &binding) {
    if (binding.size() != 64) return {};
    for (const auto c : binding)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return {};
    return QStringLiteral("devices/autoHomeOnConnect/v1/") + binding;
}
bool preferenceFileReadableWithoutSharedCache(QString *error) {
    // fileName() does not mutate settings. Do not sync/value/setValue on this
    // shared handle: Qt may have cached a partial parse of a malformed INI.
    const auto path = QSettings().fileName();
    const QFileInfo info(path);
    if (!info.isAbsolute() || (!info.exists() && info.isSymLink()) ||
        (info.exists() && !info.isFile())) {
        if (error) *error = "Einstellungen nicht lesbar; Home-Option wurde nicht geändert.";
        return false;
    }
    if (!info.exists()) return true; // Ordinary first local preference write.
    constexpr qint64 maximumSettingsBytes = 1024 * 1024;
    QFile original(path);
    if (!original.open(QIODevice::ReadOnly) || original.size() > maximumSettingsBytes) {
        if (error) *error = "Einstellungen nicht lesbar oder größer als 1 MiB; Home-Option wurde nicht geändert.";
        return false;
    }
    const auto bytes = original.read(maximumSettingsBytes + 1);
    if (original.error() != QFileDevice::NoError || !original.atEnd() ||
        bytes.size() > maximumSettingsBytes) {
        if (error) *error = "Einstellungen nicht vollständig lesbar; Home-Option wurde nicht geändert.";
        return false;
    }
    original.close();
    QTemporaryDir isolated;
    if (!isolated.isValid()) {
        if (error) *error = "Einstellungen nicht sicher prüfbar; Home-Option wurde nicht geändert.";
        return false;
    }
    const auto copyPath = isolated.filePath("settings.ini");
    QFile copy(copyPath);
    if (!copy.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        copy.write(bytes) != bytes.size() || !copy.flush()) {
        if (error) *error = "Einstellungen nicht sicher prüfbar; Home-Option wurde nicht geändert.";
        return false;
    }
    copy.close();
    // Unique filename means an actually fresh parser, not the application-wide
    // QSettings cache. Force lazy parsing of every section before any write.
    QSettings parsed(copyPath, QSettings::IniFormat);
    parsed.setFallbacksEnabled(false);
    parsed.sync();
    (void)parsed.allKeys();
    if (parsed.status() != QSettings::NoError) {
        if (error) *error = "Einstellungen nicht lesbar; Home-Option wurde nicht geändert.";
        return false;
    }
    return true;
}
std::optional<bool> strictPreference(const QVariant &value) {
    if (value.metaType().id() == QMetaType::Bool) return value.toBool();
    // NativeFormat on Linux is INI: Qt may reopen its own boolean as text.
    // Only these exact round-trip tokens are accepted, never generic toBool().
    if (value.metaType().id() == QMetaType::QString) {
        if (value.toString() == "true") return true;
        if (value.toString() == "false") return false;
    }
    return {};
}
} // namespace
bool DeviceManager::readAutoHomeOnConnect(QString *error) const {
    if (error) error->clear();
    const auto key = autoHomeKey(bindingKey());
    if (key.isEmpty()) return false;
    QSettings settings;
    settings.setFallbacksEnabled(false);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = "Einstellungen nicht lesbar; automatische Referenzfahrt bleibt ausgeschaltet.";
        return false;
    }
    const bool present=settings.contains(key);
    const auto stored=present?settings.value(key):QVariant();
    if(settings.status()!=QSettings::NoError) {
        if(error)*error="Einstellungen nicht lesbar; automatische Referenzfahrt bleibt ausgeschaltet.";
        return false;
    }
    if (!present) return false;
    const auto value = strictPreference(stored);
    if (!value) {
        if (error) *error = "Gespeicherte Home-Option ungültig; automatische Referenzfahrt bleibt ausgeschaltet.";
        return false;
    }
    return *value;
}
bool DeviceManager::autoHomeOnConnect() const { return readAutoHomeOnConnect(); }
void DeviceManager::setAutoHomeOnConnect(bool enabled) {
    if (changing) {
        lastMessage = "Geräteänderung läuft; Home-Option wurde nicht geändert.";
        return; // The enclosing mutation will notify; never recurse through changed.
    }
    QScopedValueRollback<bool> mutation(changing, true);
    if (connectionRequestPending() || machine->requestPending() || machine->occupied() ||
        machine->busy() || machine->firing()) {
        fail("Home-Option während einer offenen Anfrage oder Geräteaktion unverändert lassen.");
        return;
    }
    const auto key = autoHomeKey(bindingKey());
    if (key.isEmpty()) { fail("Home-Option benötigt ein ausgewähltes, unverändert zugeordnetes Gerät."); return; }
    QString preflightError;
    if (!preferenceFileReadableWithoutSharedCache(&preflightError)) {
        fail(preflightError); return;
    }
    QSettings settings;
    settings.setFallbacksEnabled(false);
    settings.setAtomicSyncRequired(true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        fail("Einstellungen nicht lesbar; Home-Option wurde nicht geändert."); return;
    }
    const bool existed = settings.contains(key);
    const auto previous = settings.value(key);
    if(settings.status()!=QSettings::NoError) {
        fail("Einstellungen nicht lesbar; Home-Option wurde nicht geändert.");return;
    }
    const auto parsed = strictPreference(previous);
    if ((existed && parsed && *parsed == enabled) || (!existed && !enabled)) return;
    settings.setValue(key, enabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // Undo pending cache state too: a later destructor/retry may not persist
        // an enabling value after this call has reported failure.
        if (existed) settings.setValue(key, previous); else settings.remove(key);
        settings.sync();
        fail("Home-Option konnte nicht gespeichert werden; keine Verbindung oder Fahrt ausgelöst.");
        return;
    }
    lastMessage = enabled
        ? "Automatische Referenzfahrt beim nächsten ausdrücklichen Verbinden gewählt. Jetzt wurde nichts verbunden oder bewegt."
        : "Automatische Referenzfahrt beim nächsten Verbinden ausgeschaltet. Jetzt wurde nichts verbunden oder bewegt.";
    emit changed();
}

DeviceManager::DeviceManager(Document *d, Machine *m, QString path, QObject *parent)
    : QObject(parent), document(d), machine(m), library(std::move(path)) {
    connect(&library, &DeviceLibrary::changed, this, &DeviceManager::changed);
    connect(machine, &Simulator::changed, this, &DeviceManager::changed);
    connect(machine, &Machine::connectionRequestFinished, this,
            [this](quint64 requestId, bool opened, const QString &reason) {
                if (!pendingConnectionRequestId || requestId != pendingConnectionRequestId)
                    return;
                const bool currentBinding = bindingKey() == pendingConnectionBinding;
                pendingConnectionRequestId = 0;
                pendingConnectionBinding.clear();
                // The worker/facade also rejects stale epochs. A late UI result must not
                // overwrite a different selected identity's message, even for the same model.
                if (currentBinding) {
                    lastMessage =
                        opened ? "Anschluss geöffnet. Controllerprüfung läuft; Maschinenreferenz, "
                                 "Koordinaten und Absaugung benötigen eigene Nachweise."
                               : (reason.isEmpty() ? "Anschluss konnte nicht geöffnet werden. "
                                                     "Keine Verbindung bestätigt."
                                                   : reason);
                }
                emit changed();
            });
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
QString DeviceManager::connectionStatusText() const {
    const auto phase = machine->connectionPhase();
    if (phase == "closing")
        return "Verbindung wird beendet. Gerätezuordnung erst nach bestätigter Trennung ändern.";
    if (connectionRequestPending() || phase == "opening")
        return "Verbindungsanfrage läuft. Anschlussöffnung und Controllerprüfung sind noch nicht "
               "bestätigt.";
    if (machine->simulation())
        return "Simulatorbetrieb. Keine reale Verbindung oder Maschinenfreigabe.";
    if (phase == "handshake")
        return "Anschluss geöffnet; Controllerprüfung läuft. Keine Bewegungsfreigabe.";
    if (phase == "fault")
        return "Verbindung oder Controllerprüfung fehlgeschlagen. Status prüfen; keine "
               "automatische Wiederverbindung.";
    if (!machine->connected())
        return "Reale Verbindung getrennt. Erneutes Verbinden muss ausdrücklich angefordert "
               "werden.";
    if (!machine->statusFresh())
        return "Controllerdaten fehlen oder sind veraltet. Keine aktuelle Bereitschaft bestätigt.";
    if (machine->connectionVerified())
        return "Controllerprüfung bestätigt. Referenz, Fahrraum und Zubehör bleiben eigenständige "
               "Freigaben.";
    return "Realer Anschluss geöffnet; keine vollständige aktuelle Controllerprüfung bestätigt.";
}
QString DeviceManager::changeBlockReason() const {
    if (changing) return "Geräteänderung läuft. Bitte warten.";
    if (!library.writable()) return library.message();
    if (machine->occupied() || machine->requestPending())
        return "Gerätezuordnung gesperrt: Verbindungs- oder Geräteaktion läuft. Abschluss oder "
               "bestätigte Trennung abwarten.";
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
    pendingConnectionRequestId = 0;
    pendingConnectionBinding.clear();
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
        if (!document->outputs(art)) continue;
        QVariantList extraOperations;
        for (const auto operation : art.extraOperations) extraOperations.append(operation);
        const QVariantMap values{{"kind", art.kind}, {"layer", art.layer},
                                 {"extraOperations", extraOperations},
                                 {"outline", art.engraveOutline}, {"settings", art.settings},
                                 {"style", art.style}, {"relief", art.relief.toMap()}};
        signatures.insert(QString::fromUtf8(QJsonDocument::fromVariant(values).toJson(QJsonDocument::Compact)));
    }
    auto sorted = signatures.values(); std::sort(sorted.begin(), sorted.end());
    const QVariantMap parameters{{"contract", 2},
                                {"cutSelectedGraphics", document->cutSelectedGraphics()},
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
    QString preferenceError;
    readAutoHomeOnConnect(&preferenceError);
    if (!preferenceError.isEmpty()) lastMessage += ' ' + preferenceError;
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
    if (connectionRequestPending() || machine->occupied() || machine->requestPending() ||
        machine->busy() || machine->firing() || document->planningBusy() || document->batchBusy() ||
        document->imageExportBusy())
        return fail("Verbindung während einer offenen Anfrage, Geräteaktion, Berechnung oder eines "
                    "Exports gesperrt.");
    Mutation mutation(*this);
    const auto endpoint = active()["endpoint"].toString();
    if (endpoint.isEmpty()) return fail("Für dieses Gerät fehlt der lokale Anschluss. In der Geräteverwaltung eintragen.");
    const auto error = DeviceBackends::runtimeSupportError(*library.profile(selectedId));
    if (!error.isEmpty()) return fail(error);
    const auto binding = bindingKey();
    QString preferenceError;
    const bool autoHome = readAutoHomeOnConnect(&preferenceError);
    const auto submission = machine->requestConnectHardware(endpoint, binding.toUtf8(), autoHome);
    if (!submission.enqueued)
        return fail(submission.reason.isEmpty()
                        ? "Verbindungsanfrage nicht angenommen; kein Anschluss geöffnet."
                        : submission.reason);
    pendingConnectionRequestId = submission.requestId;
    pendingConnectionBinding = binding;
    lastMessage = autoHome
        ? "Verbindungsanfrage mit anschließender Referenzfahrt nach frischer Prüfung angenommen. Anschlussöffnung, Prüfung und Referenzfahrt sind noch nicht bestätigt."
        : "Verbindungsanfrage angenommen. Anschlussöffnung und Controllerprüfung sind noch nicht bestätigt.";
    if (!preferenceError.isEmpty()) lastMessage += ' ' + preferenceError;
    emit changed(); return true;
}
