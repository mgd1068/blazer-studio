#include "devicelibrary.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
namespace {
constexpr qint64 maximumBytes = 8 * 1024 * 1024;
constexpr int maximumEntries = 100, maximumRevision = 1000000000;
QByteArray hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool text(const QString &value, int limit, bool required) {
    if (value.size() > limit || (required && value.trimmed().isEmpty()))
        return false;
    for (QChar c : value)
        if (c.category() == QChar::Other_Control)
            return false;
    return true;
}
bool keys(const QJsonObject &object, const QStringList &expected) {
    if (object.size() != expected.size())
        return false;
    for (const auto &key : expected)
        if (!object.contains(key))
            return false;
    return true;
}
} // namespace
std::optional<DeviceLibrary::Entry> DeviceLibrary::validate(QString id, QString name,
                                                            const DeviceProfile &profile,
                                                            QString endpoint, int revision,
                                                            QString &error) {
    const QUuid uuid(id);
    if (uuid.isNull() || !text(name, 100, true) || !text(endpoint, 512, false) ||
        endpoint.contains("://") || revision < 1 || revision > maximumRevision) {
        error = "Ungültige Gerätekennung, Name, lokaler Anschluss oder Bindungsrevision.";
        return {};
    }
    const auto clean = DeviceBackends::projectProfile(profile, error);
    if (!clean)
        return {};
    return Entry{uuid.toString(QUuid::WithoutBraces), name.trimmed(), endpoint.trimmed(), *clean,
                 revision};
}
QJsonObject DeviceLibrary::json(const Entry &entry) {
    return {{"id", entry.id},
            {"name", entry.name},
            {"endpoint", entry.endpoint},
            {"profile", entry.profile.toJson()},
            {"bindingRevision", entry.bindingRevision}};
}
DeviceLibrary::DeviceLibrary(QString path, QObject *parent)
    : QObject(parent), m_path(std::move(path)) {
    if (m_path.isEmpty())
        m_path =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/devices.json";
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        m_blocked = true;
        fail("Geräteverzeichnis nicht beschreibbar.");
        return;
    }
    m_lock = std::make_unique<QLockFile>(m_path + ".lock");
    if (!m_lock->tryLock(0)) {
        m_blocked = true;
        fail("Gerätebibliothek wird bereits verwendet oder ist nicht beschreibbar.");
        return;
    }
    m_existed = QFileInfo::exists(m_path);
    if (!m_existed)
        return; // Do not invent a physical device or persist an implicit selection.
    QFile file(m_path);
    auto invalid = [&] {
        m_blocked = true;
        fail("Ungültige Gerätebibliothek; Originaldatei bleibt erhalten.");
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) {
        invalid();
        return;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        invalid();
        return;
    }
    m_diskHash = hash(bytes);
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    if (!keys(root, {"format", "version", "instances"}) ||
        root["format"] != "blazer-device-library" || !root["version"].isDouble() ||
        root["version"].toDouble() != 1 || !root["instances"].isArray() ||
        root["instances"].toArray().size() > maximumEntries) {
        invalid();
        return;
    }
    QList<Entry> parsed;
    QSet<QString> ids;
    for (const auto &value : root["instances"].toArray()) {
        if (!value.isObject()) {
            invalid();
            return;
        }
        const auto record = value.toObject();
        if (!keys(record, {"id", "name", "endpoint", "profile", "bindingRevision"}) ||
            !record["id"].isString() || !record["name"].isString() ||
            !record["endpoint"].isString() || !record["profile"].isObject() ||
            !record["bindingRevision"].isDouble()) {
            invalid();
            return;
        }
        const double revision = record["bindingRevision"].toDouble();
        if (!std::isfinite(revision) || revision < 1 || revision > maximumRevision ||
            std::floor(revision) != revision) {
            invalid();
            return;
        }
        QString error;
        const auto profile =
            DeviceProfile::parse(QJsonDocument(record["profile"].toObject()).toJson(), error);
        if (!profile) {
            invalid();
            return;
        }
        auto entry = validate(record["id"].toString(), record["name"].toString(), *profile,
                              record["endpoint"].toString(), int(revision), error);
        if (!entry || ids.contains(entry->id)) {
            invalid();
            return;
        }
        ids.insert(entry->id);
        parsed.append(*entry);
    }
    m_entries = std::move(parsed);
}
DeviceLibrary::~DeviceLibrary() = default;
bool DeviceLibrary::fail(QString text) {
    m_message = std::move(text);
    emit changed();
    return false;
}
int DeviceLibrary::indexOf(const QString &id) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id)
            return i;
    return -1;
}
QVariantMap DeviceLibrary::instance(const QString &id) const {
    const int index = indexOf(id);
    return index < 0 ? QVariantMap{} : json(m_entries[index]).toVariantMap();
}
std::optional<DeviceProfile> DeviceLibrary::profile(const QString &id) const {
    const int index = indexOf(id);
    if (index < 0)
        return {};
    return m_entries[index].profile;
}
QString DeviceLibrary::bindingKey(const QString &id) const {
    const int index = indexOf(id);
    if (index < 0)
        return {};
    const auto &entry = m_entries[index];
    return QString::fromLatin1(
        hash(QJsonDocument(QJsonArray{entry.id,
                                      QString::fromLatin1(entry.profile.outputFingerprint()),
                                      entry.bindingRevision})
                 .toJson(QJsonDocument::Compact))
            .toHex());
}
QVariantList DeviceLibrary::instances() const {
    QVariantList result;
    for (const auto &entry : m_entries) {
        const auto error = DeviceBackends::runtimeSupportError(entry.profile);
        result.append(QVariantMap{{"id", entry.id},
                                  {"name", entry.name},
                                  {"profileName", entry.profile.name()},
                                  {"driver", entry.profile.driverId()},
                                  {"width", entry.profile.workArea().width()},
                                  {"height", entry.profile.workArea().height()},
                                  {"endpointConfigured", !entry.endpoint.isEmpty()},
                                  {"runtimeSupported", error.isEmpty()},
                                  {"runtimeError", error},
                                  {"bindingKey", bindingKey(entry.id)}});
    }
    return result;
}
bool DeviceLibrary::persist(const QList<Entry> &entries) {
    if (m_blocked)
        return fail("Geschützte Gerätebibliothek wird nicht überschrieben.");
    QFile current(m_path);
    const bool exists = QFileInfo::exists(m_path);
    if (exists != m_existed ||
        (exists &&
         (!current.open(QIODevice::ReadOnly) || current.size() > maximumBytes ||
          hash(current.readAll()) != m_diskHash || current.error() != QFileDevice::NoError))) {
        m_blocked = true;
        return fail("Gerätebibliothek wurde extern verändert; bitte erneut öffnen.");
    }
    current.close();
    QJsonArray records;
    for (const auto &entry : entries)
        records.append(json(entry));
    const auto bytes = QJsonDocument(QJsonObject{{"format", "blazer-device-library"},
                                                 {"version", 1},
                                                 {"instances", records}})
                           .toJson();
    if (entries.size() > maximumEntries || bytes.size() > maximumBytes)
        return fail("Gerätebibliothek überschreitet Datenlimits.");
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
        return fail(
            "Gerätebibliothek konnte nicht gespeichert werden; vorheriger Stand bleibt erhalten.");
    m_entries = entries;
    m_existed = true;
    m_diskHash = hash(bytes);
    m_message.clear();
    emit changed();
    return true;
}
QString DeviceLibrary::add(QString name, const DeviceProfile &profile, QString endpoint) {
    if (m_entries.size() >= maximumEntries) {
        fail("Höchstens 100 Geräteinstanzen möglich.");
        return {};
    }
    QString id;
    do {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (indexOf(id) >= 0);
    QString error;
    const auto entry = validate(id, std::move(name), profile, std::move(endpoint), 1, error);
    if (!entry) {
        fail(error);
        return {};
    }
    auto entries = m_entries;
    entries.append(*entry);
    return persist(entries) ? id : QString();
}
bool DeviceLibrary::update(const QString &id, QString name, const DeviceProfile &profile,
                           QString endpoint) {
    const int index = indexOf(id);
    if (index < 0)
        return fail("Gerät nicht gefunden.");
    const auto &old = m_entries[index];
    QString error;
    auto entry =
        validate(id, std::move(name), profile, std::move(endpoint), old.bindingRevision, error);
    if (!entry)
        return fail(error);
    if (old.endpoint != entry->endpoint ||
        old.profile.outputFingerprint() != entry->profile.outputFingerprint()) {
        if (entry->bindingRevision == maximumRevision)
            return fail("Bindungsrevision ausgeschöpft; neue Geräteinstanz anlegen.");
        ++entry->bindingRevision;
    }
    auto entries = m_entries;
    entries.replace(index, *entry);
    return persist(entries);
}
QString DeviceLibrary::duplicate(const QString &id, QString name) {
    const auto original = profile(id);
    if (!original) {
        fail("Gerät nicht gefunden.");
        return {};
    }
    return add(std::move(name),
               *original); // New identity, no copied endpoint or calibration grant.
}
bool DeviceLibrary::remove(const QString &id) {
    const int index = indexOf(id);
    if (index < 0)
        return fail("Gerät nicht gefunden.");
    auto entries = m_entries;
    entries.removeAt(index);
    return persist(entries);
}
