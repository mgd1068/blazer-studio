#include "settingsbundle.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
bool exactKeys(const QJsonObject &object, const QSet<QString> &keys) {
    if (object.size() != keys.size())
        return false;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!keys.contains(it.key()))
            return false;
    return true;
}
// QJsonDocument accepts duplicate keys. This schema has globally distinct keys;
// reject duplicates, including escaped spellings, before using the parsed map.
bool uniqueKeys(const QByteArray &bytes) {
    QSet<QString> keys;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != '"')
            continue;
        const auto start = i++;
        for (; i < bytes.size(); ++i) {
            if (bytes[i] == '\\')
                ++i;
            else if (bytes[i] == '"')
                break;
        }
        const auto end = i;
        auto next = i + 1;
        while (next < bytes.size() && QByteArray(" \r\n\t").contains(bytes[next]))
            ++next;
        if (next < bytes.size() && bytes[next] == ':') {
            const auto wrapped = '[' + bytes.mid(start, end - start + 1) + ']';
            const auto key = QJsonDocument::fromJson(wrapped).array().first().toString();
            if (keys.contains(key))
                return false;
            keys.insert(key);
        }
    }
    return true;
}
} // namespace
SettingsBundle::Result SettingsBundle::validate(const QVariantMap &preferences) {
    Result result;
    const auto unit = preferences.value("lengthUnit");
    if (preferences.size() != 3 || unit.metaType().id() != QMetaType::QString ||
        (unit.toString() != "mm" && unit.toString() != "in") ||
        preferences.value("darkTheme").metaType().id() != QMetaType::Bool ||
        preferences.value("beginnerMode").metaType().id() != QMetaType::Bool) {
        result.error = "Einstellungsdatei benötigt Maßeinheit (mm/in), dunkles Design und "
                       "Einfachmodus mit gültigen Typen.";
        return result;
    }
    result.preferences = preferences;
    return result;
}
SettingsBundle::Result SettingsBundle::read(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > maximumBytes)
        return {{}, "Einstellungsdatei ist leer oder größer als 16 KiB."};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !uniqueKeys(bytes))
        return {{}, "Ungültige Einstellungsdatei oder doppelte JSON-Schlüssel."};
    const auto object = document.object();
    if (!exactKeys(object, {"schema", "version", "preferences"}) ||
        object.value("schema") != QJsonValue("blazer-ui-settings") ||
        !object.value("version").isDouble() || object.value("version").toDouble() != 1 ||
        !object.value("preferences").isObject())
        return {{}, "Nicht unterstütztes Einstellungsformat oder unbekannte Felder."};
    const auto preferences = object.value("preferences").toObject();
    if (!exactKeys(preferences, {"lengthUnit", "darkTheme", "beginnerMode"}))
        return {{}, "Unvollständige Einstellungen oder nicht erlaubte Felder."};
    return validate(preferences.toVariantMap());
}
QByteArray SettingsBundle::write(const QVariantMap &preferences) {
    if (!validate(preferences).valid())
        return {};
    return QJsonDocument(QJsonObject{{"schema", "blazer-ui-settings"},
                                     {"version", 1},
                                     {"preferences", QJsonObject::fromVariantMap(preferences)}})
        .toJson(QJsonDocument::Indented);
}
