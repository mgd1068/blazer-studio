#include "materialtestledger.h"
#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
class MaterialTestLedgerTest : public QObject {
    Q_OBJECT
    const QString deviceA = QString(64, 'a'), deviceB = QString(64, 'b');
    const QString parameterA = QString(64, 'c'), parameterB = QString(64, 'd');
    QVariantMap observation() const {
        return {{"date", QDate::currentDate().toString(Qt::ISODate)},
                {"material", "Birch plywood"},
                {"batch", "batch 42"},
                {"thicknessMm", 3.0},
                {"result", "inconclusive"},
                {"notes", "User observation: charred edge; no hardware acceptance implied."}};
    }
    QByteArray bytes(const QString &path) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
  private slots:
    void persistentRecordsAreHistoricalAndStrictlyDeviceAndParameterBound() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json");
        QString first;
        {
            MaterialTestLedger ledger(path);
            QVERIFY(ledger.writable());
            QVERIFY(ledger.records().isEmpty());
            QVERIFY(!QFile::exists(path)); // Never invent a device/test on startup.
            first = ledger.record(deviceA, parameterA, observation());
            QVERIFY(!first.isEmpty());
            QVERIFY(!ledger.record(deviceB, parameterA, observation()).isEmpty());
            QVERIFY(!ledger.record(deviceA, parameterB, observation()).isEmpty());
            QCOMPARE(ledger.applicable(deviceA, parameterA).size(), 1);
            QCOMPARE(ledger.applicable(deviceA, parameterA).first().toMap()["id"].toString(),
                     first);
            QVERIFY(ledger.applicable(QString(64, 'e'), parameterA).isEmpty());
            QVERIFY(ledger.applicable(deviceA, QString(64, 'f')).isEmpty());
            const auto permissions = QFile::permissions(path);
            QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                     QFileDevice::ReadOther | QFileDevice::WriteOther)));
        }
        MaterialTestLedger loaded(path);
        QCOMPARE(loaded.records().size(), 3);
        QCOMPARE(loaded.applicable(deviceA, parameterA).first().toMap()["id"].toString(), first);
        // A new revision/duplicate identity never inherits matching observations.
        QVERIFY(loaded.applicable(QString(64, '0'), parameterA).isEmpty());
        QVERIFY(loaded.message().isEmpty());
    }
    void malformedAndUnknownContextCannotCreateTestedRecords() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json");
        MaterialTestLedger ledger(path);
        for (const auto &binding : QStringList{"", "unknown", QString(64, 'A'), QString(63, 'a'),
                                               QString(64, 'a') + "\n"})
            QVERIFY(ledger.record(binding, parameterA, observation()).isEmpty());
        QVERIFY(ledger.record(deviceA, "", observation()).isEmpty());
        for (const auto &field :
             QStringList{"date", "material", "batch", "thicknessMm", "result", "notes"}) {
            auto value = observation();
            value.remove(field);
            QVERIFY2(ledger.record(deviceA, parameterA, value).isEmpty(), qPrintable(field));
        }
        for (int variant = 0; variant < 10; ++variant) {
            auto value = observation();
            if (variant == 0)
                value["date"] = "2026-02-30";
            if (variant == 1)
                value["date"] = QDate::currentDate().addDays(1).toString(Qt::ISODate);
            if (variant == 2)
                value["thicknessMm"] = 0;
            if (variant == 3)
                value["thicknessMm"] = "3";
            if (variant == 4)
                value["notes"] = "   ";
            if (variant == 5)
                value["result"] = "accepted-hardware";
            if (variant == 6)
                value["proofConfirmed"] = true;
            if (variant == 7)
                value["notes"] = QString(4001, 'x');
            if (variant == 8)
                value["photoUrl"] = "https://example.com/photo.jpg";
            if (variant == 9)
                value["photoUrl"] = "file://remote-server/photo.jpg";
            QVERIFY2(ledger.record(deviceA, parameterA, value).isEmpty(),
                     qPrintable(QString::number(variant)));
        }
        QVERIFY(ledger.records().isEmpty());
        QVERIFY(!QFile::exists(path));
    }
    void concurrentAndExternalWritesPreserveOriginalAndBlockStaleMatches() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json");
        MaterialTestLedger first(path);
        QVERIFY(!first.record(deviceA, parameterA, observation()).isEmpty());
        const auto original = bytes(path);
        MaterialTestLedger second(path);
        QVERIFY(!second.writable());
        QVERIFY(second.record(deviceB, parameterB, observation()).isEmpty());
        QCOMPARE(bytes(path), original);
        QFile external(path);
        QVERIFY(external.open(QIODevice::WriteOnly));
        external.write("external replacement");
        external.close();
        QVERIFY(first.record(deviceA, parameterA, observation()).isEmpty());
        QVERIFY(!first.writable());
        QVERIFY(first.applicable(deviceA, parameterA).isEmpty());
        QCOMPARE(bytes(path), QByteArray("external replacement"));
    }
    void invalidAndDuplicateRecordFilesAreNeverOverwritten() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json");
        QByteArray valid;
        {
            MaterialTestLedger ledger(path);
            QVERIFY(!ledger.record(deviceA, parameterA, observation()).isEmpty());
            valid = bytes(path);
        }
        for (int variant = 0; variant < 4; ++variant) {
            auto root = QJsonDocument::fromJson(valid).object();
            auto records = root["records"].toArray();
            if (variant == 0)
                records.append(records.first());
            if (variant == 1) {
                auto entry = records.first().toObject();
                entry["bindingKey"] = "unknown";
                records[0] = entry;
            }
            if (variant == 2)
                root["grantsHardwareAccess"] = true;
            if (variant == 3) {
                auto entry = records.first().toObject();
                auto value = entry["observation"].toObject();
                value["thicknessMm"] = false;
                entry["observation"] = value;
                records[0] = entry;
            }
            root["records"] = records;
            const auto invalid = QJsonDocument(root).toJson();
            {
                QFile output(path);
                QVERIFY(output.open(QIODevice::WriteOnly));
                output.write(invalid);
            }
            MaterialTestLedger loaded(path);
            QVERIFY(!loaded.writable());
            QVERIFY(loaded.records().isEmpty());
            QVERIFY(loaded.record(deviceA, parameterA, observation()).isEmpty());
            QCOMPARE(bytes(path), invalid);
        }
    }
    void capacityAndOversizedFilesFailWithoutLosingHistory() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json");
        QByteArray valid;
        {
            MaterialTestLedger ledger(path);
            QVERIFY(!ledger.record(deviceA, parameterA, observation()).isEmpty());
            valid = bytes(path);
        }
        auto root = QJsonDocument::fromJson(valid).object();
        const auto prototype = root["records"].toArray().first().toObject();
        QJsonArray records;
        for (int i = 0; i < 1000; ++i) {
            auto item = prototype;
            item["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            records.append(item);
        }
        root["records"] = records;
        const auto full = QJsonDocument(root).toJson();
        {
            QFile output(path);
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write(full);
        }
        {
            MaterialTestLedger ledger(path);
            QVERIFY(ledger.writable());
            QCOMPARE(ledger.records().size(), 1000);
            QVERIFY(ledger.record(deviceA, parameterA, observation()).isEmpty());
            QCOMPARE(ledger.records().size(), 1000);
            QCOMPARE(bytes(path), full);
        }
        {
            QFile output(path);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QVERIFY(output.resize(8 * 1024 * 1024 + 1));
        }
        MaterialTestLedger oversized(path);
        QVERIFY(!oversized.writable());
        QVERIFY(oversized.records().isEmpty());
        QVERIFY(oversized.record(deviceA, parameterA, observation()).isEmpty());
        QCOMPARE(QFileInfo(path).size(), 8 * 1024 * 1024 + 1);
    }
    void photoIsOnlyALocalReferenceAndMissingHistoricalPhotoIsRetained() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tests.json"), photo = directory.filePath("photo.png");
        {
            QFile output(photo);
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write("reference only, not an image decoding test");
        }
        {
            MaterialTestLedger ledger(path);
            auto value = observation();
            value["photoUrl"] = QUrl::fromLocalFile(photo).toString();
            QVERIFY(!ledger.record(deviceA, parameterA, value).isEmpty());
        }
        QVERIFY(QFile::remove(photo));
        MaterialTestLedger loaded(path);
        QVERIFY(loaded.writable());
        QCOMPARE(loaded.records().size(), 1);
        auto value = observation();
        value["photoUrl"] = QUrl::fromLocalFile(photo).toString();
        QVERIFY(loaded.record(deviceA, parameterA, value).isEmpty());
    }
};
QTEST_GUILESS_MAIN(MaterialTestLedgerTest)
#include "materialtestledger_test.moc"
