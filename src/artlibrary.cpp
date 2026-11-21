#include "artlibrary.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
namespace {
constexpr qint64 maximumBytes = 64 * 1024 * 1024, maximumPixels = 32 * 1024 * 1024;
constexpr int maximumEntries = 256, maximumElements = 1000000;
QByteArray hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool keys(const QJsonObject &row, const QStringList &expected) {
    if (row.size() != expected.size())
        return false;
    for (const auto &key : expected)
        if (!row.contains(key))
            return false;
    return true;
}
bool text(const QString &value, int maximum, bool required) {
    if (value.size() > maximum || (required && value.trimmed().isEmpty()))
        return false;
    for (const auto c : value)
        if (c.category() == QChar::Other_Control)
            return false;
    return true;
}
bool uuid(const QString &value) {
    return !QUuid(value).isNull() && QUuid(value).toString(QUuid::WithoutBraces) == value;
}
bool date(const QString &value) {
    if (value.size() != 24)
        return false;
    const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    return date.isValid() && date.timeSpec() == Qt::UTC &&
           date.toString(Qt::ISODateWithMs) == value;
}
QString now() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
} // namespace
bool ArtLibrary::normalize(QString &name, QStringList &tags) {
    name = name.trimmed();
    if (!text(name, 100, true) || tags.size() > 16)
        return false;
    QStringList clean;
    QSet<QString> found;
    for (auto tag : tags) {
        tag = tag.trimmed().normalized(QString::NormalizationForm_C);
        if (!text(tag, 64, true))
            return false;
        const auto key = tag.toCaseFolded();
        if (!found.contains(key)) {
            found.insert(key);
            clean.append(tag);
        }
    }
    tags = clean;
    return true;
}
QVariantMap ArtLibrary::metadata(const Entry &entry) {
    return {{"id", entry.id},
            {"name", entry.name},
            {"tags", entry.tags},
            {"createdUtc", entry.createdUtc},
            {"updatedUtc", entry.updatedUtc},
            {"contentHash", QString::fromLatin1(entry.hash)},
            {"widthMm", entry.size.width()},
            {"heightMm", entry.size.height()},
            {"objectCount", entry.objects},
            {"conversionNotice",
             "Konturen/PNG: Füllung, Strichdarstellung, Text-/Filter-Editierbarkeit und "
             "Bearbeitungsparameter werden nicht übernommen."}};
}
ArtLibrary::ArtLibrary(QString value, QObject *parent)
    : QObject(parent), path(std::move(value)),
      libraryId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    if (path.isEmpty())
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
               "/art-library.blazerart";
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail("Grafikbibliotheksverzeichnis nicht beschreibbar.", true);
        return;
    }
    lock = std::make_unique<QLockFile>(path + ".lock");
    if (!lock->tryLock(0)) {
        fail("Grafikbibliothek wird bereits verwendet oder ist nicht beschreibbar.", true);
        return;
    }
    existed = QFileInfo::exists(path);
    if (QFileInfo(path).isSymLink() || (existed && !QFileInfo(path).isFile())) {
        fail("Grafikbibliothek muss eine reguläre lokale Datei sein.", true);
        return;
    }
    if (!existed)
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) {
        fail("Grafikbibliothek nicht lesbar oder größer als 64 MiB.", true);
        return;
    }
    const auto bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || !file.atEnd() || file.error() != QFileDevice::NoError) {
        fail("Grafikbibliothek nicht vollständig innerhalb der Grenzen lesbar.", true);
        return;
    }
    diskHash = hash(bytes);
    const auto root = QJsonDocument::fromJson(bytes).object();
    auto invalid = [&] {
        fail("Ungültige Grafikbibliothek; Originaldatei bleibt erhalten.", true);
    };
    if (!keys(root, {"format", "version", "id", "entries"}) ||
        root["format"] != "blazer-art-library" || root["version"] != 1 || !root["id"].isString() ||
        !uuid(root["id"].toString()) || !root["entries"].isArray() ||
        root["entries"].toArray().size() > maximumEntries) {
        invalid();
        return;
    }
    libraryId = root["id"].toString();
    QList<Entry> parsed;
    QSet<QString> ids;
    qint64 pixels = 0;
    int elements = 0;
    for (const auto &value : root["entries"].toArray()) {
        if (!value.isObject()) {
            invalid();
            return;
        }
        const auto row = value.toObject();
        if (!keys(row,
                  {"id", "name", "tags", "createdUtc", "updatedUtc", "contentHash", "geometry"}) ||
            !row["id"].isString() || !uuid(row["id"].toString()) ||
            ids.contains(row["id"].toString()) || !row["name"].isString() ||
            !row["tags"].isArray() || !row["createdUtc"].isString() ||
            !row["updatedUtc"].isString() || !date(row["createdUtc"].toString()) ||
            !date(row["updatedUtc"].toString()) || !row["contentHash"].isString() ||
            !row["geometry"].isObject()) {
            invalid();
            return;
        }
        QStringList tags;
        for (const auto &tag : row["tags"].toArray()) {
            if (!tag.isString()) {
                invalid();
                return;
            }
            tags.append(tag.toString());
        }
        auto name = row["name"].toString();
        const auto originalName = name;
        const auto originalTags = tags;
        if (!normalize(name, tags) || name != originalName || tags != originalTags) {
            invalid();
            return;
        }
        const auto geometry = GeometryAssetCodec::decode(
            QJsonDocument(row["geometry"].toObject()).toJson(QJsonDocument::Compact));
        if (!geometry.valid() ||
            row["contentHash"].toString() != QString::fromLatin1(geometry.contentHash)) {
            invalid();
            return;
        }
        pixels += geometry.pixelCount;
        elements += geometry.elementCount;
        if (pixels > maximumPixels || elements > maximumElements) {
            invalid();
            return;
        }
        parsed.append({row["id"].toString(), name, row["createdUtc"].toString(),
                       row["updatedUtc"].toString(), tags, geometry.canonicalBytes,
                       geometry.contentHash, geometry.boundsMm.size(), int(geometry.items.size()),
                       geometry.elementCount, geometry.pixelCount});
        ids.insert(row["id"].toString());
    }
    items = std::move(parsed);
}
ArtLibrary::~ArtLibrary() = default;
bool ArtLibrary::fail(QString message, bool protect) {
    blocked = blocked || protect;
    lastMessage = std::move(message);
    emit changed();
    return false;
}
int ArtLibrary::index(const QString &id) const {
    for (int i = 0; i < items.size(); ++i)
        if (items[i].id == id)
            return i;
    return -1;
}
QVariantList ArtLibrary::entries() const {
    QVariantList rows;
    for (const auto &entry : items)
        rows.append(metadata(entry));
    return rows;
}
QVariantList ArtLibrary::search(const QString &query) const {
    QVariantList rows;
    const auto key = query.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded();
    if (key.size() > 256)
        return rows;
    for (const auto &entry : items) {
        bool matches =
            key.isEmpty() ||
            entry.name.normalized(QString::NormalizationForm_C).toCaseFolded().contains(key);
        for (const auto &tag : entry.tags)
            matches = matches || tag.toCaseFolded().contains(key);
        if (matches)
            rows.append(metadata(entry));
    }
    return rows;
}
QByteArray ArtLibrary::geometry(const QString &id) const {
    const int i = index(id);
    return blocked || i < 0 ? QByteArray{} : items[i].geometry;
}
QByteArray ArtLibrary::thumbnail(const QString &id) {
    const int i = index(id);
    if (blocked || i < 0)
        return {};
    const auto key = QString::fromLatin1(items[i].hash);
    if (const auto cached = previews.object(key))
        return *cached;
    QString error;
    const auto png = GeometryAssetCodec::thumbnail(items[i].geometry, error);
    if (png.isEmpty()) {
        fail(error);
        return {};
    }
    previews.insert(key, new QByteArray(png));
    return png;
}
bool ArtLibrary::persist(const QList<Entry> &next) {
    if (blocked)
        return fail("Geschützte Grafikbibliothek wird nicht überschrieben.");
    QFile current(path);
    const bool exists = QFileInfo::exists(path);
    if (QFileInfo(path).isSymLink() || exists != existed ||
        (exists && (!current.open(QIODevice::ReadOnly) || current.size() > maximumBytes ||
                    hash(current.read(maximumBytes + 1)) != diskHash || !current.atEnd() ||
                    current.error() != QFileDevice::NoError)))
        return fail("Grafikbibliothek wurde extern verändert; erneut öffnen.", true);
    current.close();
    QJsonArray rows;
    qint64 pixels = 0;
    int elements = 0;
    qint64 geometryBytes = 0;
    for (const auto &entry : next) {
        pixels += entry.pixels;
        elements += entry.elements;
        geometryBytes += entry.geometry.size();
    }
    if (next.size() > maximumEntries || pixels > maximumPixels || elements > maximumElements ||
        geometryBytes > maximumBytes)
        return fail("Grafikbibliothek überschreitet Objekt-, Pixel-, Geometrie- oder Datenlimits.");
    for (const auto &entry : next) {
        rows.append(QJsonObject{{"id", entry.id},
                                {"name", entry.name},
                                {"tags", QJsonArray::fromStringList(entry.tags)},
                                {"createdUtc", entry.createdUtc},
                                {"updatedUtc", entry.updatedUtc},
                                {"contentHash", QString::fromLatin1(entry.hash)},
                                {"geometry", QJsonDocument::fromJson(entry.geometry).object()}});
    }
    if (next.size() > maximumEntries || pixels > maximumPixels || elements > maximumElements)
        return fail("Grafikbibliothek überschreitet Objekt-, Pixel- oder Geometriegrenzen.");
    const auto bytes = QJsonDocument(QJsonObject{{"format", "blazer-art-library"},
                                                 {"version", 1},
                                                 {"id", libraryId},
                                                 {"entries", rows}})
                           .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumBytes)
        return fail("Grafikbibliothek überschreitet 64 MiB.");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
        return fail("Grafikbibliothek konnte nicht atomar gespeichert werden; vorheriger Stand "
                    "bleibt erhalten.");
    items = next;
    diskHash = hash(bytes);
    existed = true;
    lastMessage = "Grafikbibliothek gespeichert; keine Bearbeitung aktiviert.";
    emit changed();
    return true;
}
QString ArtLibrary::add(QString name, QStringList tags, const QByteArray &bytes) {
    if (blocked || items.size() >= maximumEntries || !normalize(name, tags)) {
        fail("Name, Tags oder Bibliothekszustand ungültig.");
        return {};
    }
    const auto decoded = GeometryAssetCodec::decode(bytes);
    if (!decoded.valid()) {
        fail(decoded.error);
        return {};
    }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto time = now();
    auto next = items;
    next.append({id, std::move(name), time, time, std::move(tags), decoded.canonicalBytes,
                 decoded.contentHash, decoded.boundsMm.size(), int(decoded.items.size()),
                 decoded.elementCount, decoded.pixelCount});
    return persist(next) ? id : QString{};
}
bool ArtLibrary::updateMetadata(const QString &id, QString name, QStringList tags) {
    const int i = index(id);
    if (i < 0 || !normalize(name, tags))
        return fail("Grafikname oder Tags ungültig.");
    auto next = items;
    next[i].name = std::move(name);
    next[i].tags = std::move(tags);
    next[i].updatedUtc = now();
    return persist(next);
}
bool ArtLibrary::remove(const QString &id) {
    const int i = index(id);
    if (i < 0)
        return fail("Grafik nicht gefunden.");
    auto next = items;
    next.removeAt(i);
    if (!persist(next))
        return false;
    previews.clear();
    return true;
}
