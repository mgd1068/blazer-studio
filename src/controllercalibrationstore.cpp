#include "controllercalibrationstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <cerrno>
#include <cmath>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
const QByteArray keyHeader("BLAZER-CALIBRATION-KEY-V1\n");
const QByteArray domain = QByteArrayLiteral("blazer-controller-calibration-v1") + '\0';
const QString keyName = QStringLiteral("local.key");
const QString lockName = QStringLiteral("store.lock");
constexpr int maximumDepth = 8, maximumNodes = 4096, maximumKeys = 256;
constexpr qint64 maximumPayloadBytes = 47 * 1024;
bool fail(QString *error, const char *message) {
    if (error)
        *error = QString::fromUtf8(message);
    return false;
}
bool hex(const QByteArray &value) {
    if (value.size() != 64)
        return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}
QByteArray sha(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
bool equal(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size())
        return false;
    unsigned difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference |= uchar(a[i]) ^ uchar(b[i]);
    return difference == 0;
}
bool safeText(const QString &text, qsizetype limit) {
    if (text.size() > limit || text.contains(QChar(0)))
        return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c.isHighSurrogate()) {
            if (++i >= text.size() || !text[i].isLowSurrogate())
                return false;
        } else if (c.isLowSurrogate())
            return false;
    }
    return true;
}
bool jsonValue(const QVariant &value, int depth, int &nodes, qint64 &textBytes) {
    if (++nodes > maximumNodes || depth > maximumDepth)
        return false;
    switch (value.metaType().id()) {
    case QMetaType::UnknownType:
    case QMetaType::Nullptr:
    case QMetaType::Bool:
        return true;
    case QMetaType::QString: {
        const auto text = value.toString();
        if (!safeText(text, 8192))
            return false;
        textBytes += text.toUtf8().size() + 2;
        return textBytes <= maximumPayloadBytes;
    }
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float: {
        const double number = value.toDouble();
        return std::isfinite(number) && std::abs(number) <= 9007199254740991.;
    }
    case QMetaType::QVariantMap: {
        const auto map = value.toMap();
        if (map.size() > maximumKeys)
            return false;
        for (auto it = map.begin(); it != map.end(); ++it)
            if (!safeText(it.key(), 128))
                return false;
            else {
                textBytes += it.key().toUtf8().size() + 3;
                if (textBytes > maximumPayloadBytes ||
                    !jsonValue(it.value(), depth + 1, nodes, textBytes))
                    return false;
            }
        return true;
    }
    case QMetaType::QVariantList: {
        const auto list = value.toList();
        if (list.size() > maximumNodes)
            return false;
        for (const auto &entry : list)
            if (!jsonValue(entry, depth + 1, nodes, textBytes))
                return false;
        return true;
    }
    case QMetaType::QStringList: {
        const auto list = value.toStringList();
        if (list.size() > maximumNodes)
            return false;
        for (const auto &entry : list)
            if (!jsonValue(entry, depth + 1, nodes, textBytes))
                return false;
        return true;
    }
    default:
        return false; // No QVariant coercion of bytes, paths, dates or QObjects.
    }
}
bool recordValid(const QVariantMap &map) {
    int nodes = 0;
    qint64 textBytes = 0;
    return !map.isEmpty() && jsonValue(map, 0, nodes, textBytes);
}
#ifdef Q_OS_UNIX
bool statPath(const QString &path, struct stat &s, bool &missing) {
    missing = false;
    if (::lstat(QFile::encodeName(path).constData(), &s) == 0)
        return true;
    missing = errno == ENOENT;
    return false;
}
bool fileSafe(const QString &path, bool &missing) {
    struct stat s{};
    return statPath(path, s, missing) && S_ISREG(s.st_mode) && s.st_uid == ::geteuid() &&
           (s.st_mode & 0777) == 0600 && s.st_nlink == 1;
}
bool directorySafe(const QString &path, bool create, bool &missing) {
    missing = false;
    if (path.isEmpty() || !QFileInfo(path).isAbsolute())
        return false;
    // No symlink may appear anywhere in the path, including parents. A private
    // final directory is required; normal root/owner parents may be traversed.
    QString current;
    for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
        current += '/' + part;
        struct stat s{};
        bool absent = false;
        if (!statPath(current, s, absent)) {
            if (!absent)
                return false;
            if (!create) {
                missing = true;
                return true;
            }
            if (!QDir().mkdir(current))
                return false;
            if (!statPath(current, s, absent))
                return false;
        }
        if (!S_ISDIR(s.st_mode) || (s.st_uid != ::geteuid() && s.st_uid != 0))
            return false;
        if ((s.st_mode & 0002) && !(s.st_mode & S_ISVTX))
            return false;
    }
    struct stat s{};
    bool absent = false;
    if (!statPath(path, s, absent) || s.st_uid != ::geteuid())
        return false;
    if ((s.st_mode & 0777) != 0700)
        return false;
    return true;
}
bool readPrivate(const QString &path, qint64 limit, QByteArray &bytes) {
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat s{};
    if (::fstat(fd, &s) != 0 || !S_ISREG(s.st_mode) || s.st_uid != ::geteuid() ||
        (s.st_mode & 0777) != 0600 || s.st_nlink != 1 || s.st_size < 0 || s.st_size > limit) {
        ::close(fd);
        return false;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    bytes = file.read(limit + 1);
    return file.error() == QFileDevice::NoError && bytes.size() <= limit && file.atEnd();
}
#else
bool fileSafe(const QString &, bool &missing) {
    missing = false;
    return false;
}
bool directorySafe(const QString &, bool, bool &missing) {
    missing = false;
    return false;
}
bool readPrivate(const QString &, qint64, QByteArray &) {
    return false;
}
#endif
bool atomicWrite(const QString &path, const QByteArray &bytes) {
    bool missing = false;
    if (!fileSafe(path, missing) && !missing)
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size())
        return false;
    if (!file.commit())
        return false;
    return fileSafe(path, missing);
}
QString fileName(const QByteArray &binding) {
    return QString::fromLatin1(sha(binding).toHex()) + ".json";
}
QByteArray signature(const QByteArray &payload, const QByteArray &key) {
    return QMessageAuthenticationCode::hash(domain + payload, key, QCryptographicHash::Sha256);
}
bool keyRead(const QString &directory, QByteArray &key) {
    QByteArray bytes;
    if (!readPrivate(directory + '/' + keyName, 256, bytes) ||
        bytes.size() != keyHeader.size() + 64 || !bytes.startsWith(keyHeader))
        return false;
    key = bytes.mid(keyHeader.size(), 32);
    return equal(bytes.right(32), sha(keyHeader + key));
}
bool lockDirectory(const QString &directory, QLockFile &lock) {
    const auto path = directory + '/' + lockName;
    QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
#ifdef Q_OS_UNIX
        struct stat s{};
        bool missing = false;
        if (!statPath(path, s, missing) || !S_ISREG(s.st_mode) || s.st_uid != ::geteuid() ||
            s.st_nlink != 1 || (s.st_mode & 0022))
            return false;
#else
        return false;
#endif
    }
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return false;
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
bool decode(const QByteArray &bytes, const QByteArray &binding, const QByteArray &key,
            QVariantMap &record) {
    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    const auto envelope = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject() ||
        doc.toJson(QJsonDocument::Compact) != bytes || envelope.size() != 4 ||
        envelope.value("format") != "blazer-controller-calibration" ||
        !envelope.value("version").isDouble() || envelope.value("version").toDouble() != 1 ||
        !envelope.value("payload").isString() || !envelope.value("hmac").isString())
        return false;
    const auto encoded = envelope.value("payload").toString().toLatin1();
    const auto mac = envelope.value("hmac").toString().toLatin1();
    if (!hex(mac) || encoded.size() > ControllerCalibrationStore::maximumFileBytes)
        return false;
    const auto payload = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (payload.isEmpty() || payload.size() > maximumPayloadBytes ||
        payload.toBase64() != encoded || !equal(signature(payload, key), QByteArray::fromHex(mac)))
        return false;
    const auto bodyDoc = QJsonDocument::fromJson(payload, &error);
    const auto body = bodyDoc.object();
    if (error.error != QJsonParseError::NoError || !bodyDoc.isObject() ||
        bodyDoc.toJson(QJsonDocument::Compact) != payload || body.size() != 3 ||
        !body.value("schema").isDouble() || body.value("schema").toDouble() != 1 ||
        body.value("binding") != QString::fromLatin1(binding) || !body.value("record").isObject())
        return false;
    record = body.value("record").toObject().toVariantMap();
    return recordValid(record);
}
} // namespace

ControllerCalibrationStore::ControllerCalibrationStore(QString directory) {
    if (directory.isEmpty()) {
        const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (base.isEmpty())
            return;
        directory = base + "/calibration";
    }
    if (safeText(directory, 4096) && QFileInfo(directory).isAbsolute())
        m_directory = QDir::cleanPath(directory);
}
QVariantMap ControllerCalibrationStore::load(QByteArray binding, QString *error) const {
    if (error)
        error->clear();
    if (!hex(binding)) {
        fail(error, "Ungültige lokale Kalibrierungsbindung.");
        return {};
    }
    bool missing = false;
    if (!directorySafe(m_directory, false, missing)) {
        fail(error, "Unsicheres Kalibrierungsverzeichnis.");
        return {};
    }
    if (missing)
        return {};
    const auto path = m_directory + '/' + fileName(binding);
    if (!fileSafe(path, missing)) {
        if (!missing)
            fail(error, "Unsichere Kalibrierungsdatei.");
        return {};
    }
    QLockFile lock(m_directory + '/' + lockName);
    if (!lockDirectory(m_directory, lock)) {
        fail(error, "Kalibrierungsspeicher bereits belegt oder unsicher.");
        return {};
    }
    QByteArray key, bytes;
    QVariantMap result;
    if (!keyRead(m_directory, key) || !readPrivate(path, maximumFileBytes, bytes) ||
        !decode(bytes, binding, key, result)) {
        fail(error,
             "Kalibrierungsdaten oder lokaler Schlüssel ungültig; Einrichtung erneut prüfen.");
        return {};
    }
    return result;
}
bool ControllerCalibrationStore::save(QByteArray binding, QVariantMap record, QString *error) {
    if (error)
        error->clear();
    if (!hex(binding) || !recordValid(record))
        return fail(error, "Ungültige oder zu große Kalibrierungsdaten.");
    const auto payload = QJsonDocument(QJsonObject{{"schema", 1},
                                                   {"binding", QString::fromLatin1(binding)},
                                                   {"record", QJsonObject::fromVariantMap(record)}})
                             .toJson(QJsonDocument::Compact);
    if (payload.size() > maximumPayloadBytes)
        return fail(error, "Kalibrierungsdaten überschreiten das Größenlimit.");
    bool missing = false;
    const bool existed = QFileInfo::exists(m_directory);
    // Create only an absent directory; never chmod an existing unsafe one.
    if (!existed && !m_directory.isEmpty() && QFileInfo(m_directory).isAbsolute()) {
        // Check existing parent chain before mkdir; directorySafe creates no files.
        bool absent = false;
        if (!directorySafe(m_directory, false, absent))
            return fail(error, "Unsicherer Kalibrierungspfad.");
        if (!QDir().mkpath(m_directory) ||
            !QFile::setPermissions(m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                    QFileDevice::ExeOwner))
            return fail(error, "Kalibrierungsverzeichnis nicht anlegbar.");
    }
    if (!directorySafe(m_directory, false, missing) || missing)
        return fail(error, "Unsicheres Kalibrierungsverzeichnis.");
    QLockFile lock(m_directory + '/' + lockName);
    if (!lockDirectory(m_directory, lock))
        return fail(error, "Kalibrierungsspeicher bereits belegt oder unsicher.");
    QByteArray key;
    const auto keyPath = m_directory + '/' + keyName;
    if (!fileSafe(keyPath, missing)) {
        if (!missing)
            return fail(error, "Lokaler Kalibrierungsschlüssel ist unsicher.");
        const auto names =
            QDir(m_directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        if (names != QStringList{lockName})
            return fail(error, "Kalibrierungsschlüssel fehlt bei vorhandenem Datenbestand.");
        key.resize(32);
        for (auto &byte : key)
            byte = char(QRandomGenerator::system()->generate() & 255);
        if (!atomicWrite(keyPath, keyHeader + key + sha(keyHeader + key)))
            return fail(error, "Kalibrierungsschlüssel nicht atomar speicherbar.");
    } else if (!keyRead(m_directory, key))
        return fail(error, "Lokaler Kalibrierungsschlüssel ist beschädigt.");
    const auto path = m_directory + '/' + fileName(binding);
    if (fileSafe(path, missing)) {
        QByteArray old;
        QVariantMap oldRecord;
        if (!readPrivate(path, maximumFileBytes, old) || !decode(old, binding, key, oldRecord))
            return fail(error, "Vorhandene Kalibrierung ungültig; Original bleibt erhalten.");
    } else if (!missing)
        return fail(error, "Unsichere Kalibrierungsdatei.");
    const auto bytes =
        QJsonDocument(QJsonObject{{"format", "blazer-controller-calibration"},
                                  {"version", 1},
                                  {"payload", QString::fromLatin1(payload.toBase64())},
                                  {"hmac", QString::fromLatin1(signature(payload, key).toHex())}})
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumFileBytes)
        return fail(error, "Kalibrierungsdatei überschreitet das Größenlimit.");
    return atomicWrite(path, bytes) ||
           fail(error, "Kalibrierung konnte nicht atomar gespeichert werden.");
}
