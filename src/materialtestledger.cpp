#include "materialtestledger.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <cmath>
namespace {
constexpr qint64 maximumBytes = 8 * 1024 * 1024;
constexpr qsizetype maximumRecords = 1000;
QByteArray hash(const QByteArray &value) {
    return QCryptographicHash::hash(value, QCryptographicHash::Sha256);
}
bool signature(const QString &value) {
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("[0-9a-f]{64}"));
    return pattern.match(value).hasMatch();
}
bool text(const QJsonValue &value, qsizetype limit, bool required) {
    if (!value.isString() || value.toString().size() > limit ||
        (required && value.toString().trimmed().isEmpty()))
        return false;
    for (const auto c : value.toString())
        if (c.category() == QChar::Other_Control && c != '\n' && c != '\t')
            return false;
    return true;
}
bool fields(const QJsonObject &object, const QStringList &required,
            const QStringList &optional = {}) {
    for (const auto &key : required)
        if (!object.contains(key))
            return false;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!required.contains(it.key()) && !optional.contains(it.key()))
            return false;
    return true;
}
bool observation(const QJsonObject &object, bool loading) {
    if (!fields(object, {"date", "material", "batch", "thicknessMm", "result", "notes"},
                {"photoUrl"}) ||
        !text(object["date"], 10, true) || !text(object["material"], 200, true) ||
        !text(object["batch"], 200, false) || !text(object["result"], 20, true) ||
        !text(object["notes"], 4000, true) || !object["thicknessMm"].isDouble())
        return false;
    const auto date = QDate::fromString(object["date"].toString(), Qt::ISODate);
    const double thickness = object["thicknessMm"].toDouble();
    if (!date.isValid() || date.toString(Qt::ISODate) != object["date"].toString() ||
        date > QDate::currentDate() || !std::isfinite(thickness) || thickness <= 0 ||
        thickness > 1000 ||
        !QStringList{"usable", "failed", "inconclusive"}.contains(object["result"].toString()))
        return false;
    if (object.contains("photoUrl")) {
        if (!text(object["photoUrl"], 4096, false))
            return false;
        if (!object["photoUrl"].toString().isEmpty()) {
            const QUrl url(object["photoUrl"].toString());
            const QFileInfo photo(url.toLocalFile());
            // Missing historical photos remain references; never read or fetch them.
            if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() ||
                !QDir::isAbsolutePath(url.toLocalFile()) || !url.query().isEmpty() ||
                !url.fragment().isEmpty() || (!loading && (!photo.isFile() || !photo.isReadable())))
                return false;
        }
    }
    return true;
}
} // namespace
MaterialTestLedger::MaterialTestLedger(QString value, QObject *parent)
    : QObject(parent), path(std::move(value)) {
    if (path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
               "/material-tests.json";
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail("Materialtest-Verzeichnis nicht beschreibbar.", true);
        return;
    }
    lock = std::make_unique<QLockFile>(path + ".lock");
    if (!lock->tryLock(0)) {
        fail("Materialtestbuch wird bereits verwendet oder ist nicht beschreibbar.", true);
        return;
    }
    existed = QFileInfo::exists(path);
    if (!existed)
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) {
        fail("Materialtestbuch nicht lesbar oder zu groß; Original bleibt erhalten.", true);
        return;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        fail("Materialtestbuch nicht vollständig lesbar.", true);
        return;
    }
    diskHash = hash(bytes);
    const auto root = QJsonDocument::fromJson(bytes).object();
    auto invalid = [&] { fail("Ungültiges Materialtestbuch; Original bleibt erhalten.", true); };
    if (!fields(root, {"format", "version", "records"}) ||
        root["format"] != "blazer-material-test-ledger" || !root["version"].isDouble() ||
        root["version"].toDouble() != 1 || !root["records"].isArray() ||
        root["records"].toArray().size() > maximumRecords) {
        invalid();
        return;
    }
    QVariantList parsed;
    QSet<QString> ids;
    for (const auto &item : root["records"].toArray()) {
        if (!item.isObject()) {
            invalid();
            return;
        }
        auto entry = item.toObject();
        if (!fields(entry, {"id", "bindingKey", "parameterHash", "observation"}) ||
            !entry["id"].isString() || QUuid(entry["id"].toString()).isNull() ||
            QUuid(entry["id"].toString()).toString(QUuid::WithoutBraces) !=
                entry["id"].toString() ||
            ids.contains(entry["id"].toString()) || !entry["bindingKey"].isString() ||
            !signature(entry["bindingKey"].toString()) || !entry["parameterHash"].isString() ||
            !signature(entry["parameterHash"].toString()) || !entry["observation"].isObject() ||
            !observation(entry["observation"].toObject(), true)) {
            invalid();
            return;
        }
        ids.insert(entry["id"].toString());
        parsed.append(entry.toVariantMap());
    }
    entries = std::move(parsed);
}
MaterialTestLedger::~MaterialTestLedger() = default;
bool MaterialTestLedger::fail(QString value, bool protect) {
    blocked = blocked || protect;
    lastMessage = std::move(value);
    emit changed();
    return false;
}
QVariantList MaterialTestLedger::applicable(const QString &binding,
                                            const QString &parameters) const {
    QVariantList found;
    if (blocked || !signature(binding) || !signature(parameters))
        return found;
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        const auto entry = it->toMap();
        if (entry["bindingKey"].toString() == binding &&
            entry["parameterHash"].toString() == parameters)
            found.append(entry);
    }
    return found;
}
bool MaterialTestLedger::persist(const QVariantList &next) {
    if (blocked)
        return fail("Geschütztes Materialtestbuch wird nicht überschrieben.");
    QFile current(path);
    const bool exists = QFileInfo::exists(path);
    if (exists != existed ||
        (exists &&
         (!current.open(QIODevice::ReadOnly) || current.size() > maximumBytes ||
          hash(current.readAll()) != diskHash || current.error() != QFileDevice::NoError)))
        return fail("Materialtestbuch wurde extern verändert; bitte erneut öffnen.", true);
    current.close();
    const auto bytes = QJsonDocument(QJsonObject{{"format", "blazer-material-test-ledger"},
                                                 {"version", 1},
                                                 {"records", QJsonArray::fromVariantList(next)}})
                           .toJson();
    if (next.size() > maximumRecords || bytes.size() > maximumBytes)
        return fail("Materialtestbuch überschreitet Datenlimits.");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) ||
        !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        output.write(bytes) != bytes.size() || !output.commit())
        return fail(
            "Materialtest konnte nicht gespeichert werden; vorheriger Stand bleibt erhalten.");
    entries = next;
    existed = true;
    diskHash = hash(bytes);
    lastMessage =
        "Beobachtung gespeichert. Historischer Materialtest, keine Geräte- oder Ausgabefreigabe.";
    emit changed();
    return true;
}
QString MaterialTestLedger::record(const QString &binding, const QString &parameters,
                                   const QVariantMap &input) {
    if (blocked) {
        fail("Geschütztes Materialtestbuch wird nicht überschrieben.");
        return {};
    }
    if (!signature(binding) || !signature(parameters)) {
        fail("Materialtest benötigt ausgewähltes physisches Gerät und gültige Parametersignatur.");
        return {};
    }
    const auto data = QJsonObject::fromVariantMap(input);
    if (!observation(data, false)) {
        fail("Datum, Material, Charge, Dicke, Ergebnis, Notiz oder lokaler Fotobezug ungültig.");
        return {};
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto next = entries;
    next.append(QVariantMap{{"id", id},
                            {"bindingKey", binding},
                            {"parameterHash", parameters},
                            {"observation", data.toVariantMap()}});
    return persist(next) ? id : QString{};
}
