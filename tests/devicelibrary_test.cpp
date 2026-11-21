#include "devicelibrary.h"
#include "document.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
class DeviceLibraryTest : public QObject {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static DeviceProfile otherProfile() {
        auto json = DeviceProfile::m3Reference().toJson();
        json["id"] = "synthetic-600";
        auto area = json["workspace"].toObject();
        area["width"] = 600;
        area["height"] = 400;
        json["workspace"] = area;
        auto driver = json["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        json["driver"] = driver;
        QString error;
        return *DeviceProfile::parse(QJsonDocument(json).toJson(), error);
    }
  private slots:
    void identitiesAndBindingsSurviveReloadWithoutImplicitDevices() {
        QTemporaryDir storage;
        const auto path = storage.filePath("devices.json");
        QString first, second, firstKey, secondKey;
        {
            DeviceLibrary library(path);
            QVERIFY(library.writable());
            QVERIFY(library.instances().isEmpty());
            QVERIFY(!QFile::exists(path));
            first = library.add("Laser left", DeviceProfile::m3Reference(), "/not/a/real/device-A");
            second =
                library.add("Laser right", DeviceProfile::m3Reference(), "/not/a/real/device-B");
            QVERIFY(!first.isEmpty());
            QVERIFY(!second.isEmpty());
            QVERIFY(first != second);
            firstKey = library.bindingKey(first);
            secondKey = library.bindingKey(second);
            QCOMPARE(firstKey.size(), 64);
            QVERIFY(firstKey != secondKey);
            const auto summary = QJsonDocument::fromVariant(library.instances()).toJson();
            QVERIFY(!summary.contains("/not/a/real/device"));
            QVERIFY(!QFile(path).permissions().testFlag(QFileDevice::ReadGroup));
            QVERIFY(!QFile(path).permissions().testFlag(QFileDevice::ReadOther));
            // Raw storage mutations are not exposed to QML before guarded runtime integration.
            QCOMPARE(library.metaObject()->indexOfMethod("remove(QString)"), -1);
        }
        {
            DeviceLibrary library(path);
            QVERIFY(library.writable());
            QCOMPARE(library.instances().size(), 2);
            QCOMPARE(library.bindingKey(first), firstKey);
            QCOMPARE(library.bindingKey(second), secondKey);
            QCOMPARE(library.instance(first)["endpoint"].toString(),
                     QString("/not/a/real/device-A"));
            QCOMPARE(library.profile(first)->outputFingerprint(),
                     DeviceProfile::m3Reference().outputFingerprint());
        }
    }
    void bindingChangesNeverReviveAnOldGrantAndDuplicatesHaveNoEndpoint() {
        QTemporaryDir storage;
        DeviceLibrary library(storage.filePath("devices.json"));
        const auto reference = DeviceProfile::m3Reference();
        const auto id = library.add("Original", reference, "COM1");
        QVERIFY(!id.isEmpty());
        const auto initial = library.bindingKey(id);
        QVERIFY(library.update(id, "Renamed", reference, "COM1"));
        QCOMPARE(library.bindingKey(id), initial);
        auto named = reference.toJson();
        named["name"] = "Profile display name";
        QString error;
        const auto display = DeviceProfile::parse(QJsonDocument(named).toJson(), error);
        QVERIFY(display);
        QVERIFY(library.update(id, "Renamed", *display, "COM1"));
        QCOMPARE(library.bindingKey(id), initial);
        QVERIFY(library.update(id, "Renamed", reference, "COM2"));
        const auto second = library.bindingKey(id);
        QVERIFY(second != initial);
        QVERIFY(library.update(id, "Renamed", reference, "COM1"));
        QVERIFY(library.bindingKey(id) != initial);
        QVERIFY(library.bindingKey(id) != second);
        const auto beforeProfile = library.bindingKey(id);
        QVERIFY(library.update(id, "Renamed", otherProfile(), "COM1"));
        QVERIFY(library.bindingKey(id) != beforeProfile);
        QVERIFY(!library.instances().first().toMap()["runtimeSupported"].toBool());
        const auto duplicate = library.duplicate(id, "Second physical laser");
        QVERIFY(!duplicate.isEmpty());
        QVERIFY(library.bindingKey(duplicate) != library.bindingKey(id));
        QVERIFY(library.instance(duplicate)["endpoint"].toString().isEmpty());
        const auto duplicateKey = library.bindingKey(duplicate);
        QVERIFY(library.remove(id));
        QVERIFY(!library.profile(id));
        QVERIFY(library.bindingKey(id).isEmpty());
        QCOMPARE(library.bindingKey(duplicate), duplicateKey);
    }
    void provenanceAndLocalIdentityNeverEnterProjectProfiles() {
        QTemporaryDir storage;
        auto json = otherProfile().toJson();
        json["provenance"] =
            QJsonObject{{"port", "/private/legacy-endpoint"}, {"note", "private provenance"}};
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY(profile);
        const auto path = storage.filePath("devices.json");
        DeviceLibrary library(path);
        const auto id = library.add("My unit", *profile, "/private/local-endpoint");
        QVERIFY(!id.isEmpty());
        QVERIFY(!read(path).contains("legacy-endpoint"));
        QVERIFY(library.profile(id)->toJson()["provenance"].toObject().isEmpty());
        Document project;
        QVERIFY(project.setDeviceProfile(*library.profile(id)));
        const auto bytes = project.projectBytes();
        QVERIFY(!bytes.contains(id.toUtf8()));
        QVERIFY(!bytes.contains("endpoint"));
        QVERIFY(!bytes.contains("bindingRevision"));
        QCOMPARE(QJsonDocument::fromJson(bytes).object()["version"].toInt(), 11);
    }
    void invalidUpdatesAndConcurrentWritersPreserveTheOriginal() {
        QTemporaryDir storage;
        const auto path = storage.filePath("devices.json");
        DeviceLibrary library(path);
        const auto id = library.add("First", DeviceProfile::m3Reference());
        QVERIFY(!id.isEmpty());
        const auto original = read(path), binding = library.bindingKey(id).toUtf8();
        DeviceLibrary competing(path);
        QVERIFY(!competing.writable());
        QVERIFY(competing.add("Other", DeviceProfile::m3Reference()).isEmpty());
        QCOMPARE(read(path), original);
        QVERIFY(!library.update(id, "Bad\nName", DeviceProfile::m3Reference(), ""));
        QVERIFY(
            !library.update(id, "Name", DeviceProfile::m3Reference(), "serial://user:secret@host"));
        auto json = DeviceProfile::m3Reference().toJson();
        auto driver = json["driver"].toObject();
        driver["id"] = "unregistered";
        json["driver"] = driver;
        QString error;
        const auto unsupported = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY(unsupported);
        QVERIFY(library.add("Unknown", *unsupported).isEmpty());
        QCOMPARE(read(path), original);
        QCOMPARE(library.bindingKey(id).toUtf8(), binding);
        QVERIFY(write(path, "external change"));
        QVERIFY(!library.update(id, "First", DeviceProfile::m3Reference(), ""));
        QVERIFY(!library.writable());
        QCOMPARE(read(path), QByteArray("external change"));
        QCOMPARE(library.bindingKey(id).toUtf8(), binding);
    }
    void invalidFilesRemainUntouched_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &name :
             {"empty", "future", "duplicate", "fraction", "boolean", "extra", "oversized"})
            QTest::newRow(name) << QString(name);
    }
    void invalidFilesRemainUntouched() {
        QFETCH(QString, fault);
        QTemporaryDir storage;
        const auto path = storage.filePath("devices.json");
        {
            DeviceLibrary library(path);
            QVERIFY(!library.add("Valid", DeviceProfile::m3Reference()).isEmpty());
        }
        auto root = QJsonDocument::fromJson(read(path)).object();
        auto records = root["instances"].toArray();
        if (fault == "future")
            root["version"] = 2;
        else if (fault == "duplicate")
            records.append(records.first());
        else if (fault == "fraction" || fault == "boolean" || fault == "extra") {
            auto entry = records.first().toObject();
            if (fault == "fraction")
                entry["bindingRevision"] = 1.5;
            else if (fault == "boolean")
                entry["bindingRevision"] = true;
            else
                entry["verified"] = true;
            records[0] = entry;
        }
        root["instances"] = records;
        auto bytes = QJsonDocument(root).toJson();
        if (fault == "empty")
            bytes.clear();
        if (fault == "oversized")
            bytes = QByteArray(8 * 1024 * 1024 + 1, ' ');
        QVERIFY(write(path, bytes));
        DeviceLibrary library(path);
        QVERIFY(!library.writable());
        QVERIFY(library.instances().isEmpty());
        QVERIFY(library.add("Do not overwrite", DeviceProfile::m3Reference()).isEmpty());
        QCOMPARE(read(path), bytes);
    }
    void limitsAndExhaustedRevisionsAreAtomic() {
        QTemporaryDir storage;
        const auto path = storage.filePath("devices.json");
        QString id;
        {
            DeviceLibrary library(path);
            id = library.add("Last revision", DeviceProfile::m3Reference(), "COM1");
        }
        auto root = QJsonDocument::fromJson(read(path)).object();
        auto records = root["instances"].toArray();
        auto entry = records[0].toObject();
        entry["bindingRevision"] = 1000000000;
        records[0] = entry;
        root["instances"] = records;
        QVERIFY(write(path, QJsonDocument(root).toJson()));
        DeviceLibrary library(path);
        const auto before = read(path);
        QVERIFY(!library.update(id, "Last revision", DeviceProfile::m3Reference(), "COM2"));
        QCOMPARE(read(path), before);
        QVERIFY(library.update(id, "Rename only", DeviceProfile::m3Reference(), "COM1"));
        for (int i = 1; i < 100; ++i)
            QVERIFY(
                !library.add(QString("Device %1").arg(i), DeviceProfile::m3Reference()).isEmpty());
        const auto full = read(path);
        QVERIFY(library.add("Too many", DeviceProfile::m3Reference()).isEmpty());
        QCOMPARE(library.instances().size(), 100);
        QCOMPARE(read(path), full);
    }
};
QTEST_MAIN(DeviceLibraryTest)
#include "devicelibrary_test.moc"
