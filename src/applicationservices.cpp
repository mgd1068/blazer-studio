#include "applicationservices.h"
#include "atomicfile.h"
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSysInfo>
ApplicationServices::ApplicationServices(Document *document, Simulator *machine,
                                         const QString &settingsFile, QObject *parent)
    : QObject(parent), m_document(document), m_machine(machine),
      m_settings(settingsFile.isEmpty()
                     ? std::make_unique<QSettings>()
                     : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)) {
    m_complete = m_settings->value("onboarding/v1/complete", false).toBool();
}
QString ApplicationServices::version() const {
    return QStringLiteral(BLAZER_VERSION);
}
QString ApplicationServices::revision() const {
    return QStringLiteral(BLAZER_REVISION);
}
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
