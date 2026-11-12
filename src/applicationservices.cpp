#include "applicationservices.h"
#include "atomicfile.h"
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
} // namespace
ApplicationServices::ApplicationServices(Document *document, Simulator *machine,
                                         const QString &settingsFile, QObject *parent)
    : QObject(parent), m_document(document), m_machine(machine),
      m_settings(settingsFile.isEmpty()
                     ? std::make_unique<QSettings>()
                     : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)) {
    m_lengthUnit = m_settings->value(lengthUnitKey,"mm").toString()=="in" ? "in" : "mm";
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
