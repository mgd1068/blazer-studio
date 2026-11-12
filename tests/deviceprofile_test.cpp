#include "controlleradapter.h"
#include "deviceprofile.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

class DeviceProfileTest : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    QByteArray bytes(const QJsonObject &object) { return QJsonDocument(object).toJson(); }
    QByteArray legacy() {
        QFile file(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/blaze-x-m3.json"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    QJsonObject parseOutput(const QByteArray &input, QProcess &process) {
        const auto path = storage.filePath("profile with spaces.json");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(input) != input.size())
            return {};
        file.close();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("DISPLAY");
        environment.remove("WAYLAND_DISPLAY");
        environment.insert("QT_QPA_PLATFORM", "not-a-platform");
        environment.insert("XDG_CONFIG_HOME", storage.filePath("unused-config"));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(BLAZER_APP), {"--validate-device-profile", path});
        if (!process.waitForFinished(5000))
            return {};
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    }
  private slots:
    void grblEncoderRequiresItsOwnParametersAndCapabilities() {
        const auto base = DeviceProfile::m3Reference().toJson();
        for (const QJsonValue value :
             QList<QJsonValue>{0, 2.5, 1000000001., true, "1000", QJsonValue::Null}) {
            auto descriptor = base;
            auto driver = descriptor["driver"].toObject();
            auto settings = driver["settings"].toObject();
            settings["powerScale"] = value;
            driver["settings"] = settings;
            descriptor["driver"] = driver;
            QString error;
            const auto profile = DeviceProfile::parse(bytes(descriptor), error);
            QVERIFY2(profile, qPrintable(error));
            QVERIFY(!ControllerAdapters::outputError(*profile).isEmpty());
            QVERIFY(!DeviceBackends::runtimeSupportError(*profile).isEmpty());
        }
        for (const QString capability : {"motion-xy", "dynamic-power"}) {
            auto descriptor = base;
            auto capabilities = descriptor["capabilities"].toObject();
            capabilities[capability] = false;
            descriptor["capabilities"] = capabilities;
            QString error;
            const auto profile = DeviceProfile::parse(bytes(descriptor), error);
            QVERIFY(profile);
            QVERIFY(!ControllerAdapters::outputError(*profile).isEmpty());
        }
    }
    void currentLegacyProfileKeepsUiAndHasIndependentIdentity() {
        QString error;
        const auto source = legacy();
        QVERIFY(!source.isEmpty());
        const auto profile = DeviceProfile::parse(source, error);
        QVERIFY2(profile, qPrintable(error));
        QCOMPARE(profile->id(), QString("blaze-x-m3"));
        QCOMPARE(profile->workArea(), QSizeF(200, 150));
        QCOMPARE(profile->driverId(), QString("grbl-serial"));
        QCOMPARE(profile->transport(), QString("serial"));
        QVERIFY(profile->declares("motion-xy"));
        QVERIFY(!profile->declares("motion-z"));
        QCOMPARE(profile->legacyUiProperties(),
                 QJsonDocument::fromJson(source).object().toVariantMap());
        QVERIFY(DeviceBackends::runtimeSupportError(*profile).isEmpty());
        QCOMPARE(profile->outputFingerprint(), DeviceProfile::m3Reference().outputFingerprint());
        const auto restored = DeviceProfile::parse(bytes(profile->toJson()), error);
        QVERIFY(restored);
        QCOMPARE(restored->toJson(), profile->toJson());
        QCOMPARE(restored->outputFingerprint(), profile->outputFingerprint());
    }
    void descriptorsCanDescribeOtherBedsHeadsAndDriversWithoutEnablingThem() {
        auto grbl = DeviceProfile::m3Reference().toJson();
        grbl["id"] = "test-grbl-600";
        grbl["name"] = "Synthetisches GRBL-Profil";
        auto workspace = grbl["workspace"].toObject();
        workspace["width"] = 600;
        workspace["height"] = 400;
        grbl["workspace"] = workspace;
        auto driver = grbl["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        settings["baudRate"] = 250000;
        driver["settings"] = settings;
        grbl["driver"] = driver;
        QString error;
        const auto large = DeviceProfile::parse(bytes(grbl), error);
        QVERIFY2(large, qPrintable(error));
        QCOMPARE(large->workArea(), QSizeF(600, 400));
        QCOMPARE(large->driverSettings()["powerScale"].toInt(), 255);
        QVERIFY(!DeviceBackends::runtimeSupportError(*large).isEmpty());
        auto galvo = grbl;
        galvo["id"] = "test-galvo";
        galvo["workspace"] = QJsonObject{
            {"units", "mm"}, {"origin", "bottom-left"}, {"width", 110}, {"height", 110}};
        galvo["laser"] = QJsonObject{{"id", "fiber-head"}, {"technology", "fiber"}};
        galvo["driver"] = QJsonObject{{"id", "test-galvo-driver"},
                                      {"transport", "usb"},
                                      {"settings", QJsonObject{{"frequencyKHz", 30}}}};
        const auto future = DeviceProfile::parse(bytes(galvo), error);
        QVERIFY2(future, qPrintable(error));
        QCOMPARE(future->workArea(), QSizeF(110, 110));
        QVERIFY(!DeviceBackends::runtimeSupportError(*future).isEmpty());
        auto co2 = grbl;
        co2["id"] = "test-co2";
        co2["laser"] =
            QJsonObject{{"id", "co2-tube"}, {"technology", "co2"}, {"nominalPowerW", 80}};
        co2["driver"] = QJsonObject{
            {"id", "test-dsp-driver"}, {"transport", "udp"}, {"settings", QJsonObject{}}};
        const auto dsp = DeviceProfile::parse(bytes(co2), error);
        QVERIFY(dsp);
        QVERIFY(!DeviceBackends::runtimeSupportError(*dsp).isEmpty());
        QCOMPARE(dsp->toJson()["laser"].toObject()["nominalPowerW"].toInt(), 80);
        QVERIFY(!dsp->driverSettings().contains(
            "powerScale")); // S values are not universal laser parameters.
    }
    void fingerprintBindsOutputParametersAndIgnoresLabelsAndClaimedVerification() {
        const auto base = DeviceProfile::m3Reference();
        const auto original = base.outputFingerprint();
        QString error;
        auto renamed = base.toJson();
        renamed["name"] = "Mein M3";
        renamed["provenance"] =
            QJsonObject{{"hardwareVerified", true}, {"source", "Nur eine Behauptung"}};
        auto profile = DeviceProfile::parse(bytes(renamed), error);
        QVERIFY(profile);
        QCOMPARE(profile->outputFingerprint(), original);
        auto changed = base.toJson();
        auto driver = changed["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 10000;
        driver["settings"] = settings;
        changed["driver"] = driver;
        profile = DeviceProfile::parse(bytes(changed), error);
        QVERIFY(profile);
        QVERIFY(profile->outputFingerprint() != original);
        QVERIFY(!DeviceBackends::runtimeSupportError(*profile).isEmpty());
        changed = base.toJson();
        auto caps = changed["capabilities"].toObject();
        caps["device-storage"] = true;
        changed["capabilities"] = caps;
        profile = DeviceProfile::parse(bytes(changed), error);
        QVERIFY(profile);
        QVERIFY(!DeviceBackends::runtimeSupportError(*profile).isEmpty());
        changed = base.toJson();
        auto laser = changed["laser"].toObject();
        laser["nominalPowerW"] = 50;
        changed["laser"] = laser;
        profile = DeviceProfile::parse(bytes(changed), error);
        QVERIFY(profile);
        QCOMPARE(profile->driverSettings(), base.driverSettings());
        QVERIFY(profile->outputFingerprint() != original);
    }
    void invalidAndExcessiveProfilesNeverBecomeDescriptors() {
        QString error;
        auto source = DeviceProfile::m3Reference().toJson();
        for (const auto &value :
             QList<QJsonValue>{-1, 0, .01, 1000001, true, "200", QJsonValue::Null}) {
            auto root = source;
            auto workspace = root["workspace"].toObject();
            workspace["width"] = value;
            root["workspace"] = workspace;
            QVERIFY(!DeviceProfile::parse(bytes(root), error));
            QVERIFY(!error.isEmpty());
        }
        for (const auto &entry : QList<QPair<QString, QJsonValue>>{{"id", "../laser"},
                                                                   {"id", "laser\n"},
                                                                   {"version", 2},
                                                                   {"capabilities", QJsonArray{}},
                                                                   {"provenance", true},
                                                                   {"name", "\n"},
                                                                   {"driver", QJsonObject{}}}) {
            auto root = source;
            root[entry.first] = entry.second;
            QVERIFY(!DeviceProfile::parse(bytes(root), error));
        }
        auto root = source;
        root["misspelled-output-setting"] = true;
        QVERIFY(!DeviceProfile::parse(bytes(root), error));
        QJsonObject nested{{"leaf", true}};
        for (int i = 0; i < 12; ++i)
            nested = {{"nested", nested}};
        root = source;
        root["provenance"] = nested;
        QVERIFY(!DeviceProfile::parse(bytes(root), error));
        QJsonArray many;
        for (int i = 0; i < 600; ++i)
            many.append(i);
        root = source;
        root["provenance"] = QJsonObject{{"many", many}};
        QVERIFY(!DeviceProfile::parse(bytes(root), error));
        QVERIFY(!DeviceProfile::parse(QByteArray(64 * 1024 + 1, ' '), error));
        QVERIFY(!DeviceProfile::parse("[]", error));
        auto old = QJsonDocument::fromJson(legacy()).object();
        old["controller"] = "OTHER";
        QVERIFY(!DeviceProfile::parse(bytes(old), error));
        old = QJsonDocument::fromJson(legacy()).object();
        old["width"] = 600;
        const auto mismatched = DeviceProfile::parse(bytes(old), error);
        QVERIFY(mismatched);
        QVERIFY(!DeviceBackends::runtimeSupportError(*mismatched).isEmpty());
    }
    void validationCliNeedsNeitherDisplayNorControllerAndHidesDriverSettings() {
        QProcess current;
        const auto result = parseOutput(legacy(), current);
        QCOMPARE(current.exitStatus(), QProcess::NormalExit);
        QCOMPARE(current.exitCode(), 0);
        QVERIFY(result["valid"].toBool());
        QVERIFY(result["runtimeSupported"].toBool());
        QCOMPARE(result["scope"].toString(), QString("descriptor"));
        auto other = DeviceProfile::m3Reference().toJson();
        other["id"] = "test-only";
        other["driver"] =
            QJsonObject{{"id", "test-driver"},
                        {"transport", "tcp"},
                        {"settings", QJsonObject{{"private-token", "do-not-print-this-value"}}}};
        QProcess future;
        const auto unsupported = parseOutput(bytes(other), future);
        QCOMPARE(future.exitCode(), 0);
        QVERIFY(unsupported["valid"].toBool());
        QVERIFY(!unsupported["runtimeSupported"].toBool());
        QVERIFY(!QJsonDocument(unsupported).toJson().contains("do-not-print-this-value"));
        QProcess bad;
        const auto invalid = parseOutput("{broken", bad);
        QCOMPARE(bad.exitCode(), 2);
        QVERIFY(!invalid["valid"].toBool());
        QVERIFY(!QDir(storage.filePath("unused-config")).exists());
    }
};
QTEST_GUILESS_MAIN(DeviceProfileTest)
#include "deviceprofile_test.moc"
