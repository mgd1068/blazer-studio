#include "materiallibrary.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

namespace {
bool stringValue(const QVariantMap &m, const QString &key, int limit, bool nonempty = false) {
    auto v = m.value(key);
    return v.metaType().id() == QMetaType::QString && v.toString().size() <= limit &&
           (!nonempty || !v.toString().trimmed().isEmpty());
}
bool numeric(const QVariantMap &m, const QString &key, double low, double high) {
    auto v = m.value(key);
    if (v.metaType().id() == QMetaType::Bool || v.metaType().id() == QMetaType::QString)
        return false;
    bool ok = false;
    double n = v.toDouble(&ok);
    return ok && std::isfinite(n) && n >= low && n <= high;
}
QVariantMap operation(bool enabled, double speed, double power, int passes) {
    return {{"enabled", enabled},
            {"speed", speed},
            {"power", power},
            {"passes", passes},
            {"interval", .1}};
}
QVariantList defaults() {
    QVariantList list{MaterialLibrary::example()};
    for (int thickness : {3, 5}) {
        auto p = MaterialLibrary::example();
        p["id"] = QString("vendor-copy-pro5w-plywood-%1").arg(thickness);
        p["name"] = QString("Sperrholz %1 mm · M3 Pro 5 W (Schnitt)").arg(thickness);
        p["model"] = "M3 Pro";
        p["thicknessMm"] = thickness;
        p["source"] = "https://de.blazexlaser.com/pages/maschineneinstellung";
        p["notes"] = "Eigene Kopie der Hersteller-Schnittvorgabe vom 09.11.2026. Quelle nennt M3 "
                     "Pro; Zuordnung zum M3 offen. Gravur ist deaktiviert; deren Felder sind "
                     "eigene Entwurfswerte, keine Herstellerangabe.";
        p["engrave"] = operation(false, 3000, 20, 1);
        p["cut"] = operation(true, thickness == 3 ? 250 : 120, 100, 1);
        list.append(p);
    }
    return list;
}
} // namespace
QVariantMap MaterialLibrary::example() {
    return {{"id", "example-plywood-3"},
            {"name", "Sperrholz 3 mm · Beispiel"},
            {"model", "M3"},
            {"moduleW", 5.},
            {"thicknessMm", 3.},
            {"source", "Eigenes Beispiel · nicht am Material geprüft"},
            {"notes", "Startwerte des Oberflächenentwurfs; keine geprüfte Materialempfehlung."},
            {"revision", 1},
            {"engrave", operation(true, 3000, 20, 1)},
            {"cut", operation(true, 600, 80, 1)}};
}
bool MaterialLibrary::validate(const QVariantMap &p, QString &error) {
    auto fail = [&error](const QString &s) {
        error = s;
        return false;
    };
    if (!stringValue(p, "id", 80, true) || !stringValue(p, "name", 160, true) ||
        !stringValue(p, "model", 80, true) || !stringValue(p, "source", 1000) ||
        !stringValue(p, "notes", 4000) || !numeric(p, "moduleW", .1, 100) ||
        !numeric(p, "thicknessMm", 0, 1000))
        return fail("Materialprofil: Name, Modell, Modul und Materialstärke prüfen.");
    if (p.contains("revision") &&
        (!numeric(p, "revision", 1, 1000000) ||
         std::floor(p["revision"].toDouble()) != p["revision"].toDouble()))
        return fail("Ungültige Profilversion.");
    for (const QString key : {"engrave", "cut"}) {
        if (p[key].metaType().id() != QMetaType::QVariantMap)
            return fail("Materialprofil enthält keine gültigen Bearbeitungen.");
        auto o = p[key].toMap();
        if (o["enabled"].metaType().id() != QMetaType::Bool || !numeric(o, "speed", 1, 100000) ||
            !numeric(o, "power", 0, 100) || !numeric(o, "passes", 1, 100) ||
            std::floor(o["passes"].toDouble()) != o["passes"].toDouble() ||
            !numeric(o, "interval", .001, 10))
            return fail("Bearbeitung: Geschwindigkeit 1–100000, Leistung 0–100 %, Durchgänge 1–100 "
                        "und Intervall 0,001–10 mm prüfen.");
    }
    error.clear();
    return true;
}
MaterialLibrary::MaterialLibrary(QString path, QObject *parent) : QObject(parent) {
    m_path = path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                  "/materials.json"
                            : path;
    m_profiles = defaults();
    QFile file(m_path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_storageBlocked = true;
        message("Bibliotheksdatei kann nicht gelesen werden; sie wird nicht überschrieben.");
        return;
    }
    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    auto root = doc.object();
    if (parseError.error != QJsonParseError::NoError || root["schemaVersion"] != 1 ||
        !root["profiles"].isArray() || root["profiles"].toArray().size() > 1000) {
        m_storageBlocked = true;
        message("Ungültige Bibliotheksdatei; Original bleibt erhalten.");
        return;
    }
    QVariantList parsed;
    QSet<QString> ids;
    for (auto entry : root["profiles"].toArray()) {
        QString error;
        auto p = entry.toObject().toVariantMap();
        if (!entry.isObject() || !validate(p, error) || ids.contains(p["id"].toString())) {
            m_storageBlocked = true;
            message("Ungültiges Materialprofil; Originaldatei bleibt erhalten.");
            return;
        }
        ids.insert(p["id"].toString());
        parsed.append(p);
    }
    m_profiles = parsed;
}
QStringList MaterialLibrary::names() const {
    QStringList names;
    for (const auto &p : m_profiles)
        names << p.toMap()["name"].toString();
    return names;
}
QVariantMap MaterialLibrary::profileAt(int row) const {
    return row >= 0 && row < m_profiles.size() ? m_profiles[row].toMap() : QVariantMap{};
}
int MaterialLibrary::indexOf(const QString &id) const {
    for (int i = 0; i < m_profiles.size(); ++i)
        if (m_profiles[i].toMap()["id"] == id)
            return i;
    return -1;
}
void MaterialLibrary::message(const QString &s) {
    m_message = s;
    emit changed();
}
bool MaterialLibrary::persist(const QVariantList &profiles) {
    if (m_storageBlocked) {
        message("Beschädigte oder unlesbare Bibliotheksdatei wird nicht überschrieben.");
        return false;
    }
    auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                           {"profiles", QJsonArray::fromVariantList(profiles)}})
                     .toJson();
    if (bytes.size() > 4 * 1024 * 1024 || !QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        message("Materialbibliothek kann nicht gespeichert werden.");
        return false;
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        message("Materialbibliothek konnte nicht gespeichert werden; vorheriger Stand bleibt "
                "erhalten.");
        return false;
    }
    return true;
}
QString MaterialLibrary::saveProfile(QVariantMap p) {
    if (p.value("id").toString().isEmpty())
        p["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    if (!validate(p, error)) {
        message(error);
        return {};
    }
    const int row = indexOf(p["id"].toString());
    if (row < 0 && m_profiles.size() >= 1000) {
        message("Maximal 1000 Materialprofile.");
        return {};
    }
    p["name"] = p["name"].toString().trimmed();
    p["revision"] = row < 0 ? 1 : m_profiles[row].toMap().value("revision", 1).toInt() + 1;
    if (p["revision"].toInt() > 1000000) {
        message("Maximale Profilversion erreicht; bitte Profil kopieren.");
        return {};
    }
    p["updatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (const QString key : {"engrave", "cut"}) {
        auto o = p[key].toMap();
        o["passes"] = o["passes"].toInt();
        p[key] = o;
    }
    auto updated = m_profiles;
    if (row < 0)
        updated.append(p);
    else
        updated[row] = p;
    if (!persist(updated))
        return {};
    m_profiles = updated;
    message("Materialprofil gespeichert.");
    return p["id"].toString();
}
QString MaterialLibrary::duplicateProfile(int row) {
    auto p = profileAt(row);
    if (p.isEmpty())
        return {};
    p["id"] = "";
    p["name"] = (p["name"].toString() + " · Kopie").left(160);
    return saveProfile(p);
}
bool MaterialLibrary::removeProfile(const QString &id) {
    int row = indexOf(id);
    if (row < 0)
        return false;
    auto updated = m_profiles;
    updated.removeAt(row);
    if (!persist(updated))
        return false;
    m_profiles = updated;
    message("Materialprofil gelöscht; bereits geöffnete Projekte behalten ihre Werte.");
    return true;
}
