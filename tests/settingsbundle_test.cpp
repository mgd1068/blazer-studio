#include "applicationservices.h"
#include "settingsbundle.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

class SettingsBundleTest : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    static QByteArray readFile(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static void writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    QVariantMap preferences{{"lengthUnit", "in"}, {"darkTheme", true}, {"beginnerMode", false}};
  private slots:
    void schemaIsTypedBoundedAndRejectsUnknownPartialOrDuplicateFields() {
        const auto bytes = SettingsBundle::write(preferences);
        QVERIFY(!bytes.isEmpty());
        QCOMPARE(SettingsBundle::read(bytes).preferences, preferences);
        const auto root = QJsonDocument::fromJson(bytes).object();
        QList<QByteArray> invalid{
            {}, "[]", "null", "{", QByteArray(SettingsBundle::maximumBytes + 1, ' ')};
        auto changed = root;
        changed["version"] = true;
        invalid.append(QJsonDocument(changed).toJson());
        changed["version"] = 2;
        invalid.append(QJsonDocument(changed).toJson());
        changed = root;
        changed["device"] = QJsonObject{{"ready", true}};
        invalid.append(QJsonDocument(changed).toJson());
        changed = root;
        changed["schema"] = "LightBurn";
        invalid.append(QJsonDocument(changed).toJson());
        for (const auto &key :
             {QString("lengthUnit"), QString("darkTheme"), QString("beginnerMode")}) {
            auto partial = preferences;
            partial.remove(key);
            changed = root;
            changed["preferences"] = QJsonObject::fromVariantMap(partial);
            invalid.append(QJsonDocument(changed).toJson());
            partial = preferences;
            partial[key] = 1;
            changed["preferences"] = QJsonObject::fromVariantMap(partial);
            invalid.append(QJsonDocument(changed).toJson());
        }
        changed = root;
        auto unknown = preferences;
        unknown["commissioned"] = true;
        changed["preferences"] = QJsonObject::fromVariantMap(unknown);
        invalid.append(QJsonDocument(changed).toJson());
        auto badUnit = preferences;
        badUnit["lengthUnit"] = "cm";
        changed["preferences"] = QJsonObject::fromVariantMap(badUnit);
        invalid.append(QJsonDocument(changed).toJson());
        invalid.append(bytes.trimmed().chopped(1) + ",\"version\":1}");
        invalid.append(QByteArray(bytes).replace("\"darkTheme\": true",
                                                 "\"darkTheme\":false,\"dark\\u0054heme\":true"));
        for (const auto &bad : invalid)
            QVERIFY2(!SettingsBundle::read(bad).valid(), bad.constData());
        auto exactLimit = bytes;
        exactLimit.append(QByteArray(SettingsBundle::maximumBytes - bytes.size(), ' '));
        QVERIFY(SettingsBundle::read(exactLimit).valid());
        QVERIFY(SettingsBundle::write(unknown).isEmpty());
    }
    void previewExplicitImportRoundtripAndExcludedState() {
        const auto ini = temporary.filePath("portable.ini");
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue("onboarding/v1/complete", true);
            settings.setValue("device/endpoint", "serial-secret");
            settings.setValue("device/physicalTestGrant", true);
            settings.setValue("unrelated/legacy", "keep-me");
            settings.sync();
        }
        Document document;
        document.newProject();
        document.addShape("rect");
        Simulator simulator;
        const auto wasConnected = simulator.connected();
        ApplicationServices service(&document, &simulator, ini);
        QVERIFY(!service.firstRun());
        QCOMPARE(service.darkTheme(), false);
        QCOMPARE(service.beginnerMode(), true);
        const auto projectBefore = document.projectBytes();
        const auto source = QUrl::fromLocalFile(temporary.filePath("incoming.json"));
        writeFile(source.toLocalFile(), SettingsBundle::write(preferences));
        const auto settingsBefore = readFile(ini);
        QSignalSpy changes(&service, &ApplicationServices::changed);
        const auto preview = service.previewSettingsImport(source);
        QVERIFY(preview.value("valid").toBool());
        QCOMPARE(preview.value("preferences").toMap(), preferences);
        QCOMPARE(preview.value("changes").toList().size(), 3);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(readFile(ini), settingsBefore);
        QCOMPARE(service.lengthUnit(), QString("mm"));
        QVERIFY(!service.importSettings(source, ""));
        QVERIFY(!service.importSettings(source, QString(64, '0')));
        QCOMPARE(readFile(ini), settingsBefore);
        changes.clear();
        QVERIFY(service.importSettings(source, preview.value("digest").toString()));
        QCOMPARE(changes.count(), 1);
        QCOMPARE(service.lengthUnit(), QString("in"));
        QVERIFY(service.darkTheme());
        QVERIFY(!service.beginnerMode());
        QCOMPARE(document.projectBytes(), projectBefore);
        QCOMPARE(simulator.connected(), wasConnected);
        QVERIFY(!service.firstRun());
        QSettings stored(ini, QSettings::IniFormat);
        QCOMPARE(stored.value("device/endpoint").toString(), QString("serial-secret"));
        QCOMPARE(stored.value("device/physicalTestGrant").toBool(), true);
        QCOMPARE(stored.value("unrelated/legacy").toString(), QString("keep-me"));
        const auto exported = QUrl::fromLocalFile(temporary.filePath("portable-export.json"));
        QVERIFY(service.exportSettings(exported));
        const auto bytes = readFile(exported.toLocalFile());
        QCOMPARE(SettingsBundle::read(bytes).preferences, preferences);
        QVERIFY(!bytes.contains("serial-secret"));
        QVERIFY(!bytes.contains("legacy"));
        QVERIFY(!bytes.contains("onboarding"));
        ApplicationServices restored(nullptr, nullptr, ini);
        QCOMPARE(restored.lengthUnit(), QString("in"));
        QVERIFY(restored.darkTheme());
        QVERIFY(!restored.beginnerMode());
        QVERIFY(restored.setDarkTheme(false));
        QVERIFY(restored.setBeginnerMode(true));
        ApplicationServices reopened(nullptr, nullptr, ini);
        QVERIFY(!reopened.darkTheme());
        QVERIFY(reopened.beginnerMode());
    }
    void changedMalformedOversizeAndUnwritableImportsPreserveExistingState() {
        const auto ini = temporary.filePath("failure.ini");
        ApplicationServices service(nullptr, nullptr, ini);
        QVERIFY(service.setLengthUnit("in"));
        const auto original = readFile(ini);
        const auto source = QUrl::fromLocalFile(temporary.filePath("failure-source.json"));
        writeFile(source.toLocalFile(), SettingsBundle::write(preferences));
        const auto preview = service.previewSettingsImport(source);
        auto other = preferences;
        other["lengthUnit"] = "mm";
        writeFile(source.toLocalFile(), SettingsBundle::write(other));
        QVERIFY(!service.importSettings(source, preview.value("digest").toString()));
        QCOMPARE(readFile(ini), original);
        for (const auto &bytes :
             {QByteArray("{malformed"), QByteArray(SettingsBundle::maximumBytes + 1, ' ')}) {
            writeFile(source.toLocalFile(), bytes);
            QVERIFY(!service.previewSettingsImport(source).value("valid").toBool());
            QVERIFY(!service.importSettings(source, preview.value("digest").toString()));
            QCOMPARE(readFile(ini), original);
        }
        writeFile(source.toLocalFile(), SettingsBundle::write(other));
        const auto valid = service.previewSettingsImport(source);
        QVERIFY(valid.value("valid").toBool());
        QVERIFY(QFile::rename(ini, ini + ".original"));
        QVERIFY(QDir().mkdir(ini));
        QVERIFY(!service.importSettings(source, valid.value("digest").toString()));
        QCOMPARE(service.lengthUnit(), QString("in"));
        QVERIFY(!service.darkTheme());
        QVERIFY(service.beginnerMode());
        QCOMPARE(readFile(ini + ".original"), original);
        QVERIFY(QDir().rmdir(ini));
        QVERIFY(QFile::rename(ini + ".original", ini));
        QVERIFY(service.importSettings(source, valid.value("digest").toString()));
        QCOMPARE(service.lengthUnit(), QString("mm"));
        QVERIFY(service.darkTheme());
    }
    void failedAtomicWriteNeverLeaksPendingValuesOnRetryOrDestruction() {
        const auto directory = temporary.filePath("read-only-target");
        QVERIFY(QDir().mkdir(directory));
        const auto ini = directory + "/preferences.ini";
        const auto source = QUrl::fromLocalFile(temporary.filePath("write-failure.json"));
        writeFile(source.toLocalFile(), SettingsBundle::write(preferences));
        QByteArray original;
        {
            ApplicationServices service(nullptr, nullptr, ini);
            QVERIFY(service.setLengthUnit("mm"));
            original = readFile(ini);
            // Ensure the file exists even when saving the default is a no-op.
            if (original.isEmpty()) {
                QVERIFY(service.setLengthUnit("in"));
                QVERIFY(service.setLengthUnit("mm"));
                original = readFile(ini);
            }
            const auto preview = service.previewSettingsImport(source);
            QVERIFY(preview.value("valid").toBool());
            const auto permissions = QFileInfo(directory).permissions();
            const auto filePermissions = QFileInfo(ini).permissions();
            auto restore = qScopeGuard([&] {
                QFile::setPermissions(directory, permissions);
                QFile::setPermissions(ini, filePermissions);
            });
            QVERIFY(QFile::setPermissions(ini, QFileDevice::ReadOwner));
            QVERIFY(
                QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
            QVERIFY(!service.importSettings(source, preview.value("digest").toString()));
            QCOMPARE(service.lengthUnit(), QString("mm"));
            QVERIFY(!service.darkTheme());
            QVERIFY(service.beginnerMode());
            QCOMPARE(readFile(ini), original);
        }
        QCOMPARE(readFile(ini), original); // QSettings destruction cannot apply rejected values.
        ApplicationServices reopened(nullptr, nullptr, ini);
        QCOMPARE(reopened.lengthUnit(), QString("mm"));
        QVERIFY(!reopened.darkTheme());
        QVERIFY(reopened.beginnerMode());
        const auto preview = reopened.previewSettingsImport(source);
        QVERIFY(reopened.importSettings(source, preview.value("digest").toString()));
        QCOMPARE(reopened.lengthUnit(), QString("in"));
    }
    void fileIdentityAndCorruptLocalSettingsAreProtected() {
        const auto ini = temporary.filePath("identity.json");
        ApplicationServices service(nullptr, nullptr, ini);
        QVERIFY(service.setLengthUnit("in"));
        const auto original = readFile(ini);
        QVERIFY(!service.exportSettings(QUrl::fromLocalFile(ini)));
        QVERIFY(!service.previewSettingsImport(QUrl::fromLocalFile(ini)).value("valid").toBool());
        const auto symlink = temporary.filePath("symlink.json");
        QVERIFY(QFile::link(ini, symlink));
        QVERIFY(!service.exportSettings(QUrl::fromLocalFile(symlink)));
        QCOMPARE(readFile(ini), original);
#ifdef Q_OS_UNIX
        const auto hardlink = temporary.filePath("hardlink.json");
        QVERIFY(::link(QFile::encodeName(ini).constData(),
                       QFile::encodeName(hardlink).constData()) == 0);
        QVERIFY(!service.exportSettings(QUrl::fromLocalFile(hardlink)));
        QCOMPARE(readFile(ini), original);
#endif
        for (const auto &url :
             {QUrl("https://example.org/preferences.json"),
              QUrl("file://remote/tmp/preferences.json"), QUrl::fromLocalFile(ini + "?query")})
            QVERIFY(!service.exportSettings(url));
        const auto corrupt = temporary.filePath("corrupt.ini");
        writeFile(corrupt, "[broken\nthis is malformed\n");
        const auto broken = readFile(corrupt);
        ApplicationServices damaged(nullptr, nullptr, corrupt);
        const auto source = QUrl::fromLocalFile(temporary.filePath("good.json"));
        writeFile(source.toLocalFile(), SettingsBundle::write(preferences));
        const auto preview = damaged.previewSettingsImport(source);
        QVERIFY(preview.value("valid").toBool());
        QVERIFY(!damaged.importSettings(source, preview.value("digest").toString()));
        QCOMPARE(readFile(corrupt), broken);
        QCOMPARE(damaged.lengthUnit(), QString("mm"));
    }
};
QTEST_MAIN(SettingsBundleTest)
#include "settingsbundle_test.moc"
