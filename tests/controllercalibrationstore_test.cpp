#include "controllercalibrationstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageAuthenticationCode>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace {
QByteArray binding(char c = 'a') {
    return QByteArray(64, c);
}
QString recordPath(const QString &dir, QByteArray b = binding()) {
    return dir + '/' +
           QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex()) +
           ".json";
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
void write(const QString &path, const QByteArray &data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
    f.close();
    QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
}
QVariantMap record(double maxX = 200) {
    return {{"schema", 1},
            {"bounds", QVariantMap{{"minX", -200.}, {"maxX", maxX}, {"minY", -150.}, {"maxY", 0.}}},
            {"homeObserved", true},
            {"notes", QString("local calibration data")},
            {"startup", QVariantList{QString(), QString()}}};
}
QByteArray signedEnvelope(QJsonObject body, const QByteArray &key) {
    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const auto mac = QMessageAuthenticationCode::hash(
                         QByteArray("blazer-controller-calibration-v1") + '\0' + payload, key,
                         QCryptographicHash::Sha256)
                         .toHex();
    return QJsonDocument(QJsonObject{{"format", "blazer-controller-calibration"},
                                     {"version", 1},
                                     {"payload", QString::fromLatin1(payload.toBase64())},
                                     {"hmac", QString::fromLatin1(mac)}})
        .toJson(QJsonDocument::Compact);
}
} // namespace
class ControllerCalibrationStoreTest : public QObject {
    Q_OBJECT
  private slots:
    void missingLoadIsReadOnlyAndEmptyWithoutError() {
        QTemporaryDir t;
        const auto dir = t.path() + "/absent";
        ControllerCalibrationStore s(dir);
        QString error = "old";
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(dir));
    }
    void privateAtomicRoundtripUpdatesAndBindsDistinctInstances() {
        QTemporaryDir t;
        const auto dir = t.path() + "/calibration";
        ControllerCalibrationStore s(dir);
        QString error;
        QVERIFY2(s.save(binding(), record(), &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(s.load(binding(), &error), record());
        QVERIFY(error.isEmpty());
        const auto key = read(dir + "/local.key");
        QVERIFY(s.load(binding('b'), &error).isEmpty());
        QVERIFY(error.isEmpty());
        QVERIFY(s.save(binding('b'), record(120), &error));
        QCOMPARE(s.load(binding('b'), &error), record(120));
        QCOMPARE(s.load(binding()), record());
        QVERIFY(s.save(binding(), record(190), &error));
        QCOMPARE(s.load(binding()), record(190));
        QCOMPARE(read(dir + "/local.key"), key);
#ifdef Q_OS_UNIX
        struct stat info{};
        QVERIFY(::stat(QFile::encodeName(dir).constData(), &info) == 0);
        QCOMPARE(info.st_mode & 0777, mode_t(0700));
        for (const auto &name : QDir(dir).entryList(QDir::Files)) {
            QVERIFY(::stat(QFile::encodeName(dir + '/' + name).constData(), &info) == 0);
            QCOMPARE(info.st_mode & 0777, mode_t(0600));
            QCOMPARE(info.st_uid, ::geteuid());
        }
#endif
        QCOMPARE(QDir(dir).entryList(QDir::Files).size(),
                 3); // key and two records; no lingering temp/lock.
    }
    void independentlySignedFixtureAndCanonicalEnvelope() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        QVERIFY(QDir().mkdir(dir));
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner));
        const QByteArray key(32, '\x11'), header("BLAZER-CALIBRATION-KEY-V1\n");
        write(dir + "/local.key",
              header + key + QCryptographicHash::hash(header + key, QCryptographicHash::Sha256));
        const auto fixture = signedEnvelope({{"schema", 1},
                                             {"binding", QString::fromLatin1(binding())},
                                             {"record", QJsonObject::fromVariantMap(record())}},
                                            key);
        write(recordPath(dir), fixture);
        ControllerCalibrationStore s(dir);
        QString error;
        QCOMPARE(s.load(binding(), &error), record());
        QVERIFY(error.isEmpty());
        auto pretty = QJsonDocument::fromJson(fixture).toJson();
        write(recordPath(dir), pretty);
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(recordPath(dir)), pretty);
    }
    void payloadMacBindingAndEnvelopeTamperingAreRejected_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 8; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void payloadMacBindingAndEnvelopeTamperingAreRejected() {
        QFETCH(int, kind);
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        const auto path = recordPath(dir);
        auto object = QJsonDocument::fromJson(read(path)).object();
        if (kind == 0)
            object["hmac"] = QString(64, '0');
        if (kind == 1) {
            auto bytes = QByteArray::fromBase64(object["payload"].toString().toLatin1());
            bytes[bytes.size() / 2] ^= 1;
            object["payload"] = QString::fromLatin1(bytes.toBase64());
        }
        if (kind == 2)
            object["version"] = 2;
        if (kind == 3)
            object["version"] = true;
        if (kind == 4)
            object["extra"] = 1;
        if (kind == 5)
            object["format"] = "other";
        if (kind == 6)
            object["payload"] = "@@@";
        if (kind == 7)
            object["hmac"] = QString(64, 'A');
        const auto damaged = QJsonDocument(object).toJson(QJsonDocument::Compact);
        write(path, damaged);
        QString error;
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!s.save(binding(), record(120), &error));
        QCOMPARE(read(path), damaged);
    }
    void copyingRecordToAnotherBindingCannotRestoreData() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        write(recordPath(dir, binding('b')), read(recordPath(dir)));
        QString error;
        QVERIFY(s.load(binding('b'), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(s.load(binding()), record());
    }
    void missingOrCorruptedKeyIsNeverRecreatedOverData() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        const auto key = read(dir + "/local.key"), original = read(recordPath(dir));
        QVERIFY(QFile::remove(dir + "/local.key"));
        QString error;
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!s.save(binding('b'), record(), &error));
        QVERIFY(!QFileInfo::exists(dir + "/local.key"));
        auto corrupt = key;
        corrupt[30] ^= 1;
        write(dir + "/local.key", corrupt);
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!s.save(binding('b'), record(), &error));
        QCOMPARE(read(dir + "/local.key"), corrupt);
        QCOMPARE(read(recordPath(dir)), original);
    }
    void validMacDoesNotBypassPayloadSchemaOrDataBudgets() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        const auto key =
            read(dir + "/local.key").mid(QByteArray("BLAZER-CALIBRATION-KEY-V1\n").size(), 32);
        for (int i = 0; i < 5; ++i) {
            QJsonObject body{{"schema", 1},
                             {"binding", QString::fromLatin1(binding())},
                             {"record", QJsonObject::fromVariantMap(record())}};
            if (i == 0)
                body["binding"] = QString::fromLatin1(binding('b'));
            if (i == 1)
                body["schema"] = 2;
            if (i == 2)
                body["extra"] = 1;
            if (i == 3)
                body["record"] = QJsonArray{};
            if (i == 4)
                body["record"] = QJsonObject{{"huge", QString(9000, 'x')}};
            write(recordPath(dir), signedEnvelope(body, key));
            QString error;
            QVERIFY(s.load(binding(), &error).isEmpty());
            QVERIFY(!error.isEmpty());
        }
    }
    void variantAndSizeValidationPreserveOriginalAtomically() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        const auto original = read(recordPath(dir));
        QList<QVariantMap> bad = {QVariantMap{},
                                  {{"x", QByteArray("bytes")}},
                                  {{"x", std::numeric_limits<double>::quiet_NaN()}},
                                  {{"x", std::numeric_limits<double>::infinity()}},
                                  {{"x", qulonglong(9007199254740992ULL)}},
                                  {{"x", QString(8193, 'x')}}};
        QVariant nested = 1;
        for (int i = 0; i < 12; ++i)
            nested = QVariantList{nested};
        bad.append({{"nested", nested}});
        QVariantList list;
        for (int i = 0; i < 4096; ++i)
            list << i;
        bad.append({{"nodes", list}});
        QVariantMap large;
        for (int i = 0; i < 9; ++i)
            large[QString::number(i)] = QString(8000, 'x');
        bad.append(large);
        for (const auto &value : bad) {
            QString error;
            QVERIFY(!s.save(binding(), value, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(read(recordPath(dir)), original);
        }
        QString error;
        QVERIFY(!s.save(QByteArray(64, 'A'), record(), &error));
        QVERIFY(!s.save("device-port", record(), &error));
        QCOMPARE(read(recordPath(dir)), original);
        write(recordPath(dir), QByteArray(ControllerCalibrationStore::maximumFileBytes + 1, 'x'));
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void simultaneousWriterIsRejectedWithoutMutation() {
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore a(dir), b(dir);
        QVERIFY(a.save(binding(), record()));
        const auto original = read(recordPath(dir));
        QLockFile held(dir + "/store.lock");
        QVERIFY(held.tryLock(0));
        QString error;
        QVERIFY(!b.save(binding(), record(120), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(b.load(binding(), &error).isEmpty());
        QCOMPARE(read(recordPath(dir)), original);
        held.unlock();
        QVERIFY(b.save(binding(), record(120), &error));
        QCOMPARE(a.load(binding()), record(120));
    }
    void symlinksHardlinksUnsafeModesAndFailuresNeverOverwriteTargets() {
#ifdef Q_OS_UNIX
        QTemporaryDir t;
        const auto dir = t.path() + "/c";
        ControllerCalibrationStore s(dir);
        QVERIFY(s.save(binding(), record()));
        const auto path = recordPath(dir), key = dir + "/local.key";
        const auto original = read(path);
        QString error;
        const auto outside = t.path() + "/outside";
        write(outside, "untouched");
        QVERIFY(QFile::remove(path));
        QVERIFY(::symlink(QFile::encodeName(outside).constData(),
                          QFile::encodeName(path).constData()) == 0);
        QVERIFY(!s.save(binding(), record(), &error));
        QVERIFY(s.load(binding(), &error).isEmpty());
        QCOMPARE(read(outside), QByteArray("untouched"));
        QVERIFY(QFile::remove(path));
        write(path, original);
        const auto linked = t.path() + "/key-hardlink";
        QVERIFY(::link(QFile::encodeName(key).constData(), QFile::encodeName(linked).constData()) ==
                0);
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!s.save(binding(), record(), &error));
        QVERIFY(QFile::remove(linked));
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ReadGroup));
        QVERIFY(s.load(binding(), &error).isEmpty());
        QVERIFY(!s.save(binding(), record(), &error));
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto alias = t.path() + "/alias";
        QVERIFY(::symlink(QFile::encodeName(dir).constData(),
                          QFile::encodeName(alias).constData()) == 0);
        ControllerCalibrationStore unsafe(alias);
        QVERIFY(!unsafe.save(binding(), record(), &error));
        QVERIFY(unsafe.load(binding(), &error).isEmpty());
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QVERIFY(!s.save(binding(), record(), &error));
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner));
        QCOMPARE(read(path), original);
#else
        QSKIP("Unix owner/permission contract");
#endif
    }
};
QTEST_GUILESS_MAIN(ControllerCalibrationStoreTest)
#include "controllercalibrationstore_test.moc"
