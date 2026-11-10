#include "materiallibrary.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

// Referencing the initializer retains the bundled catalog in static-library consumers/tests.
static void initializeCatalog() {
    Q_INIT_RESOURCE(manufacturer_catalog);
}

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
QVariantList manufacturerProfiles() {
    static const bool initialized = [] {
        initializeCatalog();
        return true;
    }();
    Q_UNUSED(initialized);
    QFile file(":/catalog/profiles/manufacturer-materials.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Bundled manufacturer catalog missing");
    const auto catalog = QJsonDocument::fromJson(file.readAll()).object();
    QVariantList list;
    static const QRegularExpression thicknessPattern(R"((\d+(?:[.,]\d+)?)\s*mm\s*$)");
    for (const auto &tableValue : catalog["tables"].toArray()) {
        auto table = tableValue.toObject();
        const int watts = table["modulePowerW"].toInt();
        const QString mode = table["operation"].toString();
        const bool engraving = mode == "engrave";
        int row = 0;
        for (const auto &entryValue : table["entries"].toArray()) {
            auto entry = entryValue.toObject();
            const QString material = entry["material"].toString();
            QString id = QString("vendor-m3pro-%1w-%2-%3").arg(watts).arg(mode).arg(++row);
            // Preserve the IDs of the two previously supplied editable profiles.
            if (watts == 5 && !engraving &&
                (material == "Sperrholz 3mm" || material == "Sperrholz 5mm"))
                id = QString("vendor-copy-pro5w-plywood-%1").arg(material.contains("3mm") ? 3 : 5);
            auto p = MaterialLibrary::example();
            p["id"] = id;
            p["name"] = QString("%1 · M3 Pro %2 W (%3)")
                            .arg(material)
                            .arg(watts)
                            .arg(engraving ? "Gravur" : "Schnitt");
            p["model"] = table["model"].toString();
            p["moduleW"] = watts;
            auto match = thicknessPattern.match(material);
            p["thicknessMm"] =
                match.hasMatch() ? match.captured(1).replace(',', '.').toDouble() : 0.;
            p["source"] = catalog["sourceUrl"].toString();
            QString notes =
                QString(
                    "Herstellerkopie vom %1 · %2. Quelle nennt M3 Pro; Zuordnung zum M3 offen. ")
                    .arg(catalog["retrievedOn"].toString(), table["sourceTitle"].toString());
            if (!match.hasMatch())
                notes += "Stärke nicht angegeben (Feld 0 als Platzhalter). ";
            if (engraving)
                notes += "Gravur-Durchgänge fehlen in der Quelle: 1 ist ein eigener editierbarer "
                         "Arbeitswert. ";
            notes += engraving ? "Schneiden ist deaktiviert; dessen Felder sind eigene Platzhalter."
                               : "Gravur ist deaktiviert; deren Felder sind eigene Platzhalter. "
                                 "Das Schnittintervall ist ohne Funktion.";
            if (watts == 5 && !engraving && material == "Sperrholz 10mm")
                notes += " Quellenauffälligkeit: 10 mm mit 1 Durchgang, 8 mm mit 6; Originalwerte "
                         "unverändert.";
            if (watts == 1)
                notes += " Hersteller empfiehlt das 1-W-Modul nicht zum Schneiden.";
            p["notes"] = notes;
            p["sourceTable"] = table["sourceTitle"].toString();
            p["sourceParameters"] = entry.toVariantMap();
            p["sourceRetrievedOn"] = catalog["retrievedOn"].toString();
            p["sourceSha256"] = catalog["sourceSha256"].toString();
            p["engrave"] = operation(false, 3000, 20, 1);
            p["cut"] = operation(false, 600, 80, 1);
            auto values =
                operation(true, entry["speedMmMin"].toDouble(), entry["powerPercent"].toDouble(),
                          engraving ? 1 : entry["passes"].toInt());
            if (engraving)
                values["interval"] = entry["intervalMm"].toDouble();
            p[mode] = values;
            QString error;
            if (!MaterialLibrary::validate(p, error))
                qFatal("Invalid bundled material profile: %s", qPrintable(error));
            list.append(p);
        }
    }
    if (list.size() != 64)
        qFatal("Incomplete bundled manufacturer catalog");
    // Keep the initial example and original two profiles at their familiar positions.
    QVariantList ordered;
    for (const QString id : {"vendor-copy-pro5w-plywood-3", "vendor-copy-pro5w-plywood-5"})
        for (const auto &p : list)
            if (p.toMap()["id"] == id)
                ordered.append(p);
    for (const auto &p : list)
        if (!ordered.contains(p))
            ordered.append(p);
    return ordered;
}
QVariantList defaults() {
    QVariantList list{MaterialLibrary::example()};
    list.append(manufacturerProfiles());
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
    // Add this catalog once to existing libraries; never replace edited user profiles.
    // The marker also keeps deliberately deleted manufacturer profiles deleted.
    if (root.value("manufacturerCatalogVersion").toInt() < 1) {
        auto merged = parsed;
        for (const auto &profile : manufacturerProfiles()) {
            auto id = profile.toMap()["id"].toString();
            if (!ids.contains(id))
                merged.append(profile);
        }
        if (merged.size() > 1000) {
            message(
                "Herstellerprofile nicht ergänzt: Bibliothek würde 1000 Profile überschreiten.");
            return;
        }
        const QString backup = m_path + ".before-manufacturer-v1.bak";
        if (!QFile::exists(backup) && !QFile::copy(m_path, backup)) {
            message("Herstellerprofile nicht ergänzt: Sicherung der bisherigen Bibliothek "
                    "fehlgeschlagen.");
            return;
        }
        if (persist(merged)) {
            m_profiles = merged;
            message("Hersteller-Materialliste ergänzt; eigene Profile und Änderungen erhalten.");
        }
    }
}
QStringList MaterialLibrary::names() const {
    QStringList names;
    for (const auto &p : m_profiles)
        names << p.toMap()["name"].toString();
    return names;
}
QVariantList MaterialLibrary::modulePowers() const {
    QList<double> powers{1., 5., 10.};
    for (const auto &p : m_profiles) {
        const auto watts = p.toMap()["moduleW"].toDouble();
        if (!powers.contains(watts))
            powers.append(watts);
    }
    std::sort(powers.begin(), powers.end());
    QVariantList result;
    for (double watts : powers)
        result.append(watts);
    return result;
}
QVariantList MaterialLibrary::rowsForModule(double watts) const {
    QVariantList rows;
    if (!std::isfinite(watts))
        return rows;
    for (int row = 0; row < m_profiles.size(); ++row)
        if (m_profiles[row].toMap()["moduleW"].toDouble() == watts)
            rows.append(row);
    return rows;
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
                                           {"manufacturerCatalogVersion", 1},
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
