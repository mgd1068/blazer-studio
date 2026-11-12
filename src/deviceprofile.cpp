#include "deviceprofile.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
constexpr qsizetype maximumBytes = 64 * 1024;
bool identifier(const QJsonValue &v) {
    static const QRegularExpression pattern(
        QRegularExpression::anchoredPattern("[a-z0-9][a-z0-9._-]{0,99}"));
    return v.isString() && pattern.match(v.toString()).hasMatch();
}
bool number(const QJsonValue &v, double minimum, double maximum) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= minimum &&
           v.toDouble() <= maximum;
}
bool bounded(const QJsonValue &v, int depth, int &nodes) {
    if (depth > 8 || ++nodes > 512)
        return false;
    if (v.isDouble())
        return std::isfinite(v.toDouble());
    if (v.isString())
        return v.toString().size() <= 16000;
    if (v.isObject()) {
        const auto object = v.toObject();
        for (auto i = object.begin(); i != object.end(); ++i)
            if (i.key().size() > 100 || !bounded(i.value(), depth + 1, nodes))
                return false;
    } else if (v.isArray())
        for (const auto &item : v.toArray())
            if (!bounded(item, depth + 1, nodes))
                return false;
    return !v.isUndefined();
}
bool keys(const QJsonObject &object, const QStringList &allowed) {
    for (auto i = object.begin(); i != object.end(); ++i)
        if (!allowed.contains(i.key()))
            return false;
    return true;
}
} // namespace

QSizeF DeviceProfile::workArea() const {
    const auto workspace = data["workspace"].toObject();
    return {workspace["width"].toDouble(), workspace["height"].toDouble()};
}
QByteArray DeviceProfile::outputFingerprint() const {
    auto output = data;
    output.remove("name");
    output.remove("provenance");
    return QCryptographicHash::hash(QJsonDocument(output).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256)
        .toHex();
}
DeviceProfile DeviceProfile::m3Reference() {
    return DeviceProfile(
        {{"format", "blazer-device-profile"},
         {"version", 1},
         {"id", "blaze-x-m3"},
         {"name", "Blaze X M3"},
         {"workspace",
          QJsonObject{{"units", "mm"}, {"origin", "bottom-left"}, {"width", 200}, {"height", 150}}},
         {"laser", QJsonObject{{"id", "diode-5w"}, {"technology", "diode"}, {"nominalPowerW", 5}}},
         {"driver", QJsonObject{{"id", "grbl-serial"},
                                {"transport", "serial"},
                                {"settings", QJsonObject{{"baudRate", 115200},
                                                         {"powerScale", 1000},
                                                         {"focusPower", 2},
                                                         {"zEnabled", false},
                                                         {"homeOnStartupConfigured", true},
                                                         {"originCode", 0}}}}},
         {"capabilities", QJsonObject{{"motion-xy", true},
                                      {"motion-z", false},
                                      {"homing", true},
                                      {"dynamic-power", true},
                                      {"focus-pulse", true},
                                      {"air-assist", false},
                                      {"device-storage", false},
                                      {"rotary", false}}},
         {"provenance", QJsonObject{}}});
}
std::optional<DeviceProfile> DeviceProfile::fromObject(QJsonObject value, QString &error) {
    auto fail = [&](const QString &reason) -> std::optional<DeviceProfile> {
        error = reason;
        return {};
    };
    int nodes = 0;
    if (!bounded(value, 0, nodes) ||
        QJsonDocument(value).toJson(QJsonDocument::Compact).size() > maximumBytes)
        return fail("Geräteprofil überschreitet Daten- oder Strukturlimits.");
    if (!keys(value, {"format", "version", "id", "name", "workspace", "laser", "driver",
                      "capabilities", "provenance"}) ||
        value["format"] != "blazer-device-profile" || !number(value["version"], 1, 1) ||
        !identifier(value["id"]))
        return fail("Unbekanntes Geräteprofilformat, Version oder Kennung.");
    if (!value["name"].isString() || value["name"].toString().trimmed().isEmpty() ||
        value["name"].toString().size() > 200)
        return fail("Geräteprofil benötigt einen Namen bis 200 Zeichen.");
    for (QChar c : value["name"].toString())
        if (c.category() == QChar::Other_Control)
            return fail("Steuerzeichen im Gerätenamen.");
    if (!value["workspace"].isObject())
        return fail("Arbeitsraum fehlt.");
    const auto workspace = value["workspace"].toObject();
    if (!keys(workspace, {"units", "origin", "width", "height"}) || workspace["units"] != "mm" ||
        workspace["origin"] != "bottom-left" || !number(workspace["width"], .1, 1000000) ||
        !number(workspace["height"], .1, 1000000))
        return fail("Ungültiger kanonischer Arbeitsraum in Millimetern.");
    if (!value["laser"].isObject())
        return fail("Laserkopf fehlt.");
    const auto laser = value["laser"].toObject();
    if (!keys(laser, {"id", "technology", "nominalPowerW"}) || !identifier(laser["id"]) ||
        !identifier(laser["technology"]) ||
        (!laser["nominalPowerW"].isUndefined() && !laser["nominalPowerW"].isNull() &&
         !number(laser["nominalPowerW"], .001, 1000000)))
        return fail("Ungültige Laserkopf-Beschreibung.");
    if (!value["driver"].isObject())
        return fail("Controllerzuordnung fehlt.");
    const auto driver = value["driver"].toObject();
    if (!keys(driver, {"id", "transport", "settings"}) || !identifier(driver["id"]) ||
        !identifier(driver["transport"]) || !driver["settings"].isObject())
        return fail("Ungültige Controllerzuordnung.");
    if (!value["capabilities"].isObject() || value["capabilities"].toObject().size() > 64)
        return fail("Ungültige Fähigkeitsliste.");
    const auto capabilities = value["capabilities"].toObject();
    for (auto i = capabilities.begin(); i != capabilities.end(); ++i)
        if (!identifier(i.key()) || !i.value().isBool())
            return fail("Fähigkeiten müssen benannte Wahrheitswerte sein.");
    if (value.contains("provenance") && !value["provenance"].isObject())
        return fail("Ungültiger Herkunftsnachweis.");
    if (!value.contains("provenance"))
        value["provenance"] = QJsonObject{};
    error.clear();
    return DeviceProfile(std::move(value));
}
std::optional<DeviceProfile> DeviceProfile::fromLegacyM3(const QJsonObject &source,
                                                         QString &error) {
    auto fail = [&](const QString &reason) -> std::optional<DeviceProfile> {
        error = reason;
        return {};
    };
    if (source["profileName"] != "M3" || source["controller"] != "GRBL" ||
        !source["name"].isString() || !number(source["width"], .1, 1000000) ||
        !number(source["height"], .1, 1000000) || !number(source["modulePowerW"], .001, 1000000) ||
        !number(source["baudRate"], 1200, 4000000) ||
        std::floor(source["baudRate"].toDouble()) != source["baudRate"].toDouble() ||
        !number(source["powerScale"], .001, 1000000000) || !number(source["focusPower"], 0, 100) ||
        !source["zEnabled"].isBool() || !source["homeOnStartupConfigured"].isBool() ||
        !number(source["originCode"], 0, 8) ||
        std::floor(source["originCode"].toDouble()) != source["originCode"].toDouble())
        return fail(
            "Ungültiges altes M3-Profil; andere Geräte benötigen das benannte Profilformat.");
    auto value = m3Reference().toJson();
    value["name"] = source["name"];
    auto workspace = value["workspace"].toObject();
    workspace["width"] = source["width"];
    workspace["height"] = source["height"];
    value["workspace"] = workspace;
    auto laser = value["laser"].toObject();
    laser["nominalPowerW"] = source["modulePowerW"];
    if (source["modulePowerW"].toDouble() != 5)
        laser["id"] =
            "legacy-module-" + QString::number(source["modulePowerW"].toDouble(), 'g', 12) + "w";
    value["laser"] = laser;
    auto driver = value["driver"].toObject();
    QJsonObject settings;
    for (const QString key : {"baudRate", "powerScale", "focusPower", "zEnabled",
                              "homeOnStartupConfigured", "originCode"})
        settings[key] = source[key];
    driver["settings"] = settings;
    value["driver"] = driver;
    auto capabilities = value["capabilities"].toObject();
    capabilities["motion-z"] = source["zEnabled"];
    value["capabilities"] = capabilities;
    value["provenance"] = QJsonObject{{"legacy", source}};
    return fromObject(std::move(value), error);
}
std::optional<DeviceProfile> DeviceProfile::parse(const QByteArray &bytes, QString &error) {
    if (bytes.size() > maximumBytes) {
        error = "Geräteprofil größer als 64 KiB.";
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "Ungültiges Geräteprofil-JSON.";
        return {};
    }
    const auto object = document.object();
    return object.contains("format") ? fromObject(object, error) : fromLegacyM3(object, error);
}
QVariantMap DeviceProfile::legacyUiProperties() const {
    auto result = data["provenance"].toObject()["legacy"].toObject();
    result["name"] = name();
    if (!result.contains("profileName"))
        result["profileName"] = id();
    result["controller"] = driverId() == "grbl-serial" ? "GRBL" : driverId();
    result["width"] = workArea().width();
    result["height"] = workArea().height();
    result["modulePowerW"] = data["laser"].toObject()["nominalPowerW"];
    const auto settings = driverSettings();
    for (auto i = settings.begin(); i != settings.end(); ++i)
        result[i.key()] = i.value();
    return result.toVariantMap();
}
QString DeviceBackends::grblOutputError(const DeviceProfile &profile) {
    if (profile.driverId() != "grbl-serial" || profile.transport() != "serial")
        return "Für dieses Profil ist kein GRBL-Ausgabeadapter vorhanden.";
    const auto scale = profile.driverSettings()["powerScale"];
    if (!number(scale, 1, 1000000000) || std::floor(scale.toDouble()) != scale.toDouble())
        return "GRBL-Ausgabe benötigt eine ganzzahlige Leistungsskala von 1 bis 1000000000.";
    if (!profile.declares("motion-xy") || !profile.declares("dynamic-power"))
        return "Der vorhandene GRBL-Ausgabeadapter benötigt XY und dynamische Leistung.";
    return {};
}
std::optional<DeviceProfile> DeviceBackends::projectProfile(const DeviceProfile &profile,
                                                            QString &error) {
    auto fail = [&](QString message) -> std::optional<DeviceProfile> {
        error = std::move(message);
        return {};
    };
    if (profile.driverId() != "grbl-serial" || profile.transport() != "serial")
        return fail("Für diesen Controller ist noch kein Projektprofilschema eingebunden.");
    const auto settings = profile.driverSettings();
    if (!keys(settings, {"baudRate", "powerScale", "focusPower", "zEnabled",
                         "homeOnStartupConfigured", "originCode"}) ||
        !number(settings["baudRate"], 1200, 4000000) ||
        std::floor(settings["baudRate"].toDouble()) != settings["baudRate"].toDouble() ||
        !number(settings["focusPower"], 0, 100) || settings["zEnabled"] != false ||
        !settings["homeOnStartupConfigured"].isBool() || settings["originCode"] != 0 ||
        !grblOutputError(profile).isEmpty())
        return fail("Projektprofil benötigt bekannte GRBL-XY-Ausgabeparameter und Ursprung links "
                    "unten; keine Anschluss- oder Zugangsdaten.");
    if (profile.workArea().width() > 2000 || profile.workArea().height() > 2000)
        return fail("Der aktuelle Editor unterstützt Arbeitsflächen bis 2000 × 2000 mm.");
    auto data = profile.toJson();
    data["provenance"] = QJsonObject{};
    return DeviceProfile::parse(QJsonDocument(data).toJson(), error);
}
QString DeviceBackends::runtimeSupportError(const DeviceProfile &profile) {
    if (profile.driverId() != "grbl-serial")
        return "Für diesen Controller ist noch kein aktiver Treiber eingebunden.";
    if (profile.outputFingerprint() != DeviceProfile::m3Reference().outputFingerprint())
        return "Dieses Profil ist noch nicht durchgängig mit Editor, Planung und Treiber "
               "verbunden; nur der bisherige M3-Laufzeitvertrag ist freigegeben.";
    return {};
}
