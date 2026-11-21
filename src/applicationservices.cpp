#include "applicationservices.h"
#include "atomicfile.h"
#include "settingsbundle.h"
#include "machine.h"
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <QCryptographicHash>
#include <QFile>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#include "blazer_version.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSysInfo>
#include <algorithm>
namespace {
constexpr auto lengthUnitKey = "editor/v1/lengthUnit";
constexpr auto darkThemeKey = "ui/v1/darkTheme";
constexpr auto beginnerModeKey = "ui/v1/beginnerMode";
constexpr auto recentKey = "projects/v1/recent";
QString localProjectKey(const QUrl &url) {
    if (!url.isValid() || !url.isLocalFile() ||
        (!url.host().isEmpty() && url.host() != "localhost") || url.hasQuery() ||
        url.hasFragment() || url.toString().size() > 32768 ||
        !QDir::isAbsolutePath(url.toLocalFile()))
        return {};
    const QFileInfo info(url.toLocalFile());
    auto path = info.canonicalFilePath();
    if (path.isEmpty())
        path = QDir::cleanPath(info.absoluteFilePath());
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}
QStringList recentList(const QVariant &value) {
    QStringList result;
    const auto candidates = value.toStringList();
    for (int i = 0; i < std::min(candidates.size(), qsizetype(1000)); ++i) {
        const auto key = localProjectKey(QUrl(candidates[i], QUrl::StrictMode));
        if (!key.isEmpty() && !result.contains(key))
            result.append(key);
        if (result.size() == 10)
            break;
    }
    return result;
}
QString bundlePath(const QUrl &url, const QString &settingsFile, QString &error) {
    if (!url.isValid() || !url.isLocalFile() ||
        (!url.host().isEmpty() && url.host() != "localhost") || url.hasQuery() ||
        url.hasFragment() || url.toString().size() > 32768 ||
        !QDir::isAbsolutePath(url.toLocalFile())) {
        error = "Bitte eine lokale absolute JSON-Datei für die Einstellungen wählen.";
        return {};
    }
    const QFileInfo file(url.toLocalFile()), settings(settingsFile);
    if (file.suffix().compare("json", Qt::CaseInsensitive) != 0 || file.isSymLink() ||
        (file.exists() && !file.isFile())) {
        error = "Bitte eine reguläre JSON-Datei ohne symbolische Verknüpfung wählen.";
        return {};
    }
    bool same = file.absoluteFilePath() == settings.absoluteFilePath() ||
                (!file.canonicalFilePath().isEmpty() &&
                 file.canonicalFilePath() == settings.canonicalFilePath());
#ifdef Q_OS_UNIX
    struct stat target{}, current{};
    if (::stat(QFile::encodeName(file.absoluteFilePath()).constData(), &target) == 0 &&
        ::stat(QFile::encodeName(settings.absoluteFilePath()).constData(), &current) == 0)
        same = same || (target.st_dev == current.st_dev && target.st_ino == current.st_ino);
#endif
    if (same) {
        error = "Die laufende Einstellungsdatei darf nicht als Austauschdatei verwendet werden.";
        return {};
    }
    return file.absoluteFilePath();
}
QByteArray readBundleFile(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Einstellungsdatei konnte nicht gelesen werden.";
        return {};
    }
    const auto bytes = file.read(SettingsBundle::maximumBytes + 1);
    if (file.error() != QFile::NoError || bytes.size() > SettingsBundle::maximumBytes) {
        error = "Einstellungsdatei ist nicht lesbar oder größer als 16 KiB.";
        return {};
    }
    return bytes;
}
} // namespace
ApplicationServices::ApplicationServices(Document *document, Simulator *machine,
                                         const QString &settingsFile, QObject *parent)
    : QObject(parent), m_document(document), m_machine(machine),
      m_settings(settingsFile.isEmpty()
                     ? std::make_unique<QSettings>()
                     : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)) {
    m_lengthUnit = m_settings->value(lengthUnitKey,"mm").toString()=="in" ? "in" : "mm";
    m_darkTheme = m_settings->value(darkThemeKey, false).toString() == "true";
    m_beginnerMode = m_settings->value(beginnerModeKey, true).toString() != "false";
    m_complete = m_settings->value("onboarding/v1/complete", false).toBool();
    if (m_settings->status() == QSettings::NoError)
        m_recent = recentList(m_settings->value(recentKey));
    if (m_document)
        connect(m_document, &Document::projectFileUsed, this, [this](const QUrl &file) {
            const auto key = localProjectKey(file);
            if (key.isEmpty())
                return;
            m_settings->sync();
            auto files = recentList(m_settings->value(recentKey));
            files.removeAll(key);
            files.prepend(key);
            while (files.size() > 10)
                files.removeLast();
            if (!storeRecentProjects(files))
                m_document->notify("Die Projektdatei wurde erfolgreich verwendet. " + m_message);
        });
}
bool ApplicationServices::storeRecentProjects(const QStringList &files) {
    if (m_settings->status() != QSettings::NoError) {
        // QSettings retains the first error even after the location is repaired.
        // A new handle lets a deliberate retry report the current outcome.
        const auto path = m_settings->fileName();
        const auto format = m_settings->format();
        m_settings.reset();
        m_settings = std::make_unique<QSettings>(path, format);
    }
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        m_message = "Der Dateiverlauf konnte nicht gespeichert werden; bitte die "
                    "Anwendungseinstellungen und Zugriffsrechte prüfen.";
        emit changed();
        return false;
    }
    const auto previous = m_settings->value(recentKey);
    m_settings->setValue(recentKey, files);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        if (previous.isValid())
            m_settings->setValue(recentKey, previous);
        else
            m_settings->remove(recentKey);
        m_message = "Der Dateiverlauf konnte nicht gespeichert werden; die Projektdatei bleibt "
                    "davon unberührt.";
        emit changed();
        return false;
    }
    m_recent = files;
    m_message.clear();
    emit changed();
    return true;
}
QVariantList ApplicationServices::recentProjects() const {
    QVariantList result;
    for (const auto &key : m_recent) {
        const QUrl url(key, QUrl::StrictMode);
        const QFileInfo info(url.toLocalFile());
        result.append(QVariantMap{{"url", url},
                                  {"name", info.fileName()},
                                  {"path", info.absoluteFilePath()},
                                  {"available", info.isFile()}});
    }
    return result;
}
bool ApplicationServices::clearRecentProjects() { return storeRecentProjects({}); }
bool ApplicationServices::openRecentProject(const QUrl &file) {
    const auto key = localProjectKey(file);
    const auto original = file.toString(QUrl::FullyEncoded);
    if (!m_document || key.isEmpty() || (!m_recent.contains(original) && !m_recent.contains(key))) {
        m_message = "Dieser Eintrag ist nicht mehr im Dateiverlauf enthalten.";
        emit changed();
        return false;
    }
    m_message.clear();
    const bool opened = m_document->open(m_recent.contains(original) ? file : QUrl(key));
    emit changed(); // Refresh availability even when opening fails.
    return opened;
}
int ApplicationServices::reclaimFileDialogResources(QObject *dialog, QObject *owner) {
    if (!dialog || !owner || dialog->parent() != owner ||
        dialog->objectName() != "saveProjectDialog" || dialog->property("visible").toBool())
        return 0;
    const auto title = dialog->property("title").toString();
    if (title.isEmpty())
        return 0;
    int reclaimed = 0;
    // Qt's Quick fallback reparents its chooser to the window. Give these
    // retired chooser objects back to their wrapper before destroying it.
    // Use the unique chooser title and its properties, not private Qt types.
    const auto children = owner->children();
    for (auto *child : children) {
        if (child == dialog || child->property("title").toString() != title ||
            !child->property("selectedFile").isValid() ||
            !child->property("currentFolder").isValid())
            continue;
        child->setParent(dialog);
        ++reclaimed;
    }
    return reclaimed;
}
QString ApplicationServices::version() const { return QStringLiteral(BLAZER_VERSION); }
QString ApplicationServices::revision() const { return QStringLiteral(BLAZER_REVISION); }
bool ApplicationServices::finishOnboarding() {
    m_settings->setValue("onboarding/v1/complete", true);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        m_message = "Der Erststart konnte nicht gespeichert werden. Bitte die Zugriffsrechte der "
                    "Anwendungsdaten prüfen.";
        emit changed();
        return false;
    }
    m_complete = true;
    m_message.clear();
    emit changed();
    return true;
}
QVariantMap ApplicationServices::diagnostics() const {
    QVariantMap system{{"os", QSysInfo::productType()},
                       {"osVersion", QSysInfo::productVersion()},
                       {"kernel", QSysInfo::kernelVersion()},
                       {"architecture", QSysInfo::currentCpuArchitecture()},
                       {"qt", QString::fromLatin1(qVersion())},
                       {"displayBackend", QGuiApplication::platformName()}};
    QVariantMap project;
    if (m_document) {
        project = {{"objects", m_document->count()},
                   {"modified", m_document->dirty()},
                   {"planningState", m_document->planningState()},
                   {"planningBusy", m_document->planningBusy()}};
        if (m_document->backgroundPlanning()) {
            auto summary = m_document->jobSummary();
            project["segments"] = summary["segments"];
            project["jobValid"] = summary["valid"];
            project["jobErrorCount"] = summary["errors"].toStringList().size();
        }
    }
    QVariantMap device;
    if (m_machine) {
        auto mode = m_machine->property("simulation");
        device = {{"simulation", !mode.isValid() || mode.toBool()},
                  {"connected", m_machine->connected()},
                  {"busy", m_machine->busy()},
                  {"paused", m_machine->paused()},
                  {"firing", m_machine->firing()}};
    }
    // Allowlist only: never include raw logs, artwork, paths, port identifiers,
    // serial numbers, network configuration, project names or material notes.
    return {{"schema", 1},
            {"application", "Blazer Studio"},
            {"version", version()},
            {"revision", revision()},
            {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"system", system},
            {"projectState", project},
            {"deviceState", device},
            {"hardwareAcceptance", "pending"},
            {"externalUsabilityTest", "pending"}};
}
bool ApplicationServices::exportDiagnostics(const QUrl &file) {
    if (!file.isLocalFile() ||
        QFileInfo(file.toLocalFile()).suffix().compare("json", Qt::CaseInsensitive) != 0) {
        m_message = "Bitte eine lokale JSON-Datei für den Diagnosebericht wählen.";
        emit changed();
        return false;
    }
    QString error;
    bool ok = AtomicFile::write(file.toLocalFile(),
                                QJsonDocument::fromVariant(diagnostics()).toJson(), error, ".bak");
    m_message = ok ? "Diagnosebericht gespeichert. Du kannst ihn vor dem freiwilligen Anhängen an "
                     "einen Fehlerbericht ansehen."
                   : error;
    emit changed();
    return ok;
}

QVariantMap ApplicationServices::previewTechnicalDiagnostics(bool includeController) {
    m_technicalDiagnosticsJson.clear();
    m_technicalDiagnosticsDigest.clear();
    auto report = diagnostics(); // Keep the existing privacy-default schema unchanged.
    if (includeController)
        if (const auto machine = qobject_cast<Machine *>(m_machine))
            report["controllerContext"] = machine->controllerDiagnosticSnapshot();
    auto bytes = QJsonDocument::fromVariant(report).toJson();
    if (bytes.isEmpty() || bytes.size() > 64 * 1024) {
        m_message = "Technische Diagnose überschreitet 64 KiB; keine exportierbare Vorschau.";
        emit changed();
        return {};
    }
    // Salt the content binding so identical subsequent previews also invalidate
    // earlier tokens without adding any metadata to the privacy-default report.
    const auto nonce = QUuid::createUuid().toRfc4122();
    m_technicalDiagnosticsDigest = QString::fromLatin1(
        QCryptographicHash::hash(nonce + bytes, QCryptographicHash::Sha256).toHex());
    m_technicalDiagnosticsJson = std::move(bytes);
    const QVariantMap result{{"digest", m_technicalDiagnosticsDigest},
                             {"report", report},
                             {"json", QString::fromUtf8(m_technicalDiagnosticsJson)}};
    m_message = report.contains("controllerContext")
        ? "Technische Vorschau mit ausdrücklich gewähltem Controllerkontext eingefroren."
        : includeController ? "Controllerkontext nicht verfügbar; Standarddiagnosevorschau eingefroren."
                            : "Diagnosevorschau ohne Controllerkontext eingefroren.";
    emit changed();
    return result;
}
bool ApplicationServices::exportTechnicalDiagnostics(const QUrl &url,
                                                     const QString &expectedDigest) {
    const auto fail = [this](const QString &message) {
        m_message = message;
        emit changed();
        return false;
    };
    const bool digestValid =
        expectedDigest.size() == 64 &&
        std::all_of(expectedDigest.cbegin(), expectedDigest.cend(),
                    [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (!digestValid || expectedDigest != m_technicalDiagnosticsDigest ||
        m_technicalDiagnosticsJson.isEmpty() || m_technicalDiagnosticsJson.size() > 64 * 1024)
        return fail(
            "Zuerst eine aktuelle technische Diagnosevorschau prüfen; Vorschautoken ungültig.");
    if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() ||
        url.hasFragment() || url.toString().size() > 32768 ||
        !QDir::isAbsolutePath(url.toLocalFile()) ||
        QFileInfo(url.toLocalFile()).suffix().compare("json", Qt::CaseInsensitive) != 0)
        return fail("Bitte eine lokale reguläre JSON-Datei für die eingefrorene Diagnose wählen.");
    const auto frozenBytes = m_technicalDiagnosticsJson;
    const auto path = url.toLocalFile();
    QLockFile lock(path + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(
            "Diagnosedatei wird bereits bearbeitet oder das Verzeichnis ist nicht beschreibbar.");
    const QFileInfo target(path);
    if (target.isSymLink() || (target.exists() && !target.isFile()))
        return fail("Diagnoseziel muss eine reguläre Datei sein; Verweise und Geräte werden nicht "
                    "überschrieben.");
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) ||
        !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        output.write(frozenBytes) != frozenBytes.size() ||
        !output.commit())
        return fail("Eingefrorene Diagnose konnte nicht atomar gespeichert werden; vorheriger "
                    "Stand bleibt erhalten.");
    m_message = "Geprüfte Diagnosevorschau unverändert lokal gespeichert; kein Upload.";
    emit changed();
    return true;
}
void ApplicationServices::clearTechnicalDiagnosticsPreview() {
    m_technicalDiagnosticsJson.clear();
    m_technicalDiagnosticsDigest.clear();
    m_message = "Technische Diagnosevorschau verworfen.";
    emit changed();
}

bool ApplicationServices::setLengthUnit(const QString &unit) {
    if (unit != "mm" && unit != "in")
        return false;
    if (m_settings->status() != QSettings::NoError) {
        const auto path = m_settings->fileName();
        const auto format = m_settings->format();
        m_settings.reset();
        m_settings = std::make_unique<QSettings>(path, format);
    }
    m_settings->sync();
    const auto previous = m_settings->value(lengthUnitKey);
    if (m_settings->status() == QSettings::NoError && unit == m_lengthUnit &&
        (!previous.isValid() || previous.toString() == unit))
        return true;
    if (m_settings->status() == QSettings::NoError) {
        m_settings->setValue(lengthUnitKey, unit);
        m_settings->sync();
        if (m_settings->status() == QSettings::NoError) {
            m_lengthUnit = unit;
            m_message.clear();
            emit changed();
            return true;
        }
        if (previous.isValid())
            m_settings->setValue(lengthUnitKey, previous);
        else
            m_settings->remove(lengthUnitKey);
    }
    m_message =
        "Die Maßeinheit konnte nicht gespeichert werden; bisherige Einheit bleibt erhalten.";
    emit changed();
    return false;
}

QVariantMap ApplicationServices::portablePreferences() const {
    return {{"lengthUnit", m_lengthUnit}, {"darkTheme", m_darkTheme},
            {"beginnerMode", m_beginnerMode}};
}
bool ApplicationServices::settingsFailure(const QString &message) {
    m_message = message;
    emit changed();
    return false;
}
bool ApplicationServices::applyPortablePreferences(const QVariantMap &preferences) {
    const auto validated = SettingsBundle::validate(preferences);
    if (!validated.valid())
        return settingsFailure(validated.error);
    // Reopening a format-error QSettings handle can rewrite its partial cache.
    // A malformed existing file must remain byte-for-byte untouched.
    if (m_settings->status() == QSettings::FormatError)
        return settingsFailure("Lokale Einstellungen sind beschädigt; Import nicht übernommen.");
    if (m_settings->status() != QSettings::NoError) {
        const auto path = m_settings->fileName();
        const auto format = m_settings->format();
        m_settings.reset();
        m_settings = std::make_unique<QSettings>(path, format);
    }
    // QSettings merges other untouched keys and commits its INI file atomically.
    // Never permit its direct-write fallback for a multi-preference transaction.
    m_settings->setAtomicSyncRequired(true);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError ||
        QFileInfo(m_settings->fileName()).isSymLink())
        return settingsFailure("Einstellungen nicht beschreibbar oder beschädigt; bisheriger Stand bleibt erhalten.");
    const QVariantMap updates{{lengthUnitKey, preferences.value("lengthUnit")},
                              {darkThemeKey, preferences.value("darkTheme")},
                              {beginnerModeKey, preferences.value("beginnerMode")}};
    QVariantMap previous;
    for (auto it = updates.begin(); it != updates.end(); ++it) {
        previous.insert(it.key(), m_settings->value(it.key()));
        m_settings->setValue(it.key(), it.value());
    }
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        // Roll pending values back too: a later QSettings destructor/retry must
        // not apply the rejected import after the filesystem becomes writable.
        for (auto it = previous.begin(); it != previous.end(); ++it)
            if (it.value().isValid())
                m_settings->setValue(it.key(), it.value());
            else
                m_settings->remove(it.key());
        m_settings->sync();
        return settingsFailure("Einstellungen konnten nicht atomar gespeichert werden; bisheriger Stand bleibt erhalten.");
    }
    m_lengthUnit = preferences.value("lengthUnit").toString();
    m_darkTheme = preferences.value("darkTheme").toBool();
    m_beginnerMode = preferences.value("beginnerMode").toBool();
    m_message = "Oberflächeneinstellungen gespeichert.";
    emit changed();
    return true;
}
bool ApplicationServices::setDarkTheme(bool enabled) {
    auto preferences = portablePreferences();
    preferences["darkTheme"] = enabled;
    return applyPortablePreferences(preferences);
}
bool ApplicationServices::setBeginnerMode(bool enabled) {
    auto preferences = portablePreferences();
    preferences["beginnerMode"] = enabled;
    return applyPortablePreferences(preferences);
}
bool ApplicationServices::exportSettings(const QUrl &file) {
    QString error;
    const auto path = bundlePath(file, m_settings->fileName(), error);
    if (path.isEmpty())
        return settingsFailure(error);
    const auto bytes = SettingsBundle::write(portablePreferences());
    if (bytes.isEmpty() || !AtomicFile::write(path, bytes, error, ".bak"))
        return settingsFailure(error.isEmpty() ? "Einstellungen konnten nicht exportiert werden." : error);
    m_message = "Oberflächeneinstellungen exportiert: Maßeinheit, Design und Einfachmodus.";
    emit changed();
    return true;
}
QVariantMap ApplicationServices::previewSettingsImport(const QUrl &file) {
    QString error;
    const auto path = bundlePath(file, m_settings->fileName(), error);
    const auto bytes = path.isEmpty() ? QByteArray() : readBundleFile(path, error);
    const auto parsed = error.isEmpty() ? SettingsBundle::read(bytes)
                                       : SettingsBundle::Result{{}, error};
    if (!parsed.valid()) {
        settingsFailure(parsed.error);
        return {{"valid", false}, {"error", parsed.error}};
    }
    QVariantList changes;
    const auto current = portablePreferences();
    const QMap<QString, QString> labels{{"lengthUnit", "Maßeinheit"},
                                        {"darkTheme", "Dunkles Design"},
                                        {"beginnerMode", "Einfachmodus"}};
    for (auto it = parsed.preferences.begin(); it != parsed.preferences.end(); ++it)
        if (current.value(it.key()) != it.value())
            changes.append(QVariantMap{{"key", it.key()}, {"label", labels.value(it.key())},
                                         {"current", current.value(it.key())}, {"incoming", it.value()}});
    return {{"valid", true}, {"error", ""},
            {"digest", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"preferences", parsed.preferences}, {"changes", changes}};
}
bool ApplicationServices::importSettings(const QUrl &file, const QString &expectedDigest) {
    if (expectedDigest.size() != 64)
        return settingsFailure("Bitte die Einstellungsdatei zuerst prüfen und die Übernahme bestätigen.");
    const auto preview = previewSettingsImport(file);
    if (!preview.value("valid").toBool())
        return false;
    if (preview.value("digest").toString() != expectedDigest)
        return settingsFailure("Einstellungsdatei wurde seit der Vorschau verändert; bitte erneut prüfen.");
    // Apply exactly the re-read, validated bytes associated with this digest.
    return applyPortablePreferences(preview.value("preferences").toMap());
}
