#include "applicationservices.h"
#include "machine.h"
#include "materiallibrary.h"
#include "recovery.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
class ReleaseTest : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    static QByteArray read(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
  private slots:
    void offlineCliLeavesUserLibraryUntouched_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("screenshot") << QString("screenshot");
        QTest::newRow("gcode-export") << QString("export");
        QTest::newRow("self-test") << QString("self-test");
    }
    void offlineCliLeavesUserLibraryUntouched() {
        QFETCH(QString, mode);
        QTemporaryDir isolated;
        const auto data = isolated.filePath("data");
        const auto libraryDirectory = data + "/BlazerStudio/Blazer Studio";
        QVERIFY(QDir().mkpath(libraryDirectory));
        const auto libraryPath = libraryDirectory + "/materials.json";
        const auto original = QJsonDocument::fromVariant(
            QVariantMap{{"schemaVersion", 1}, {"profiles", QVariantList{MaterialLibrary::example()}}})
                                  .toJson();
        QFile library(libraryPath);
        QVERIFY(library.open(QIODevice::WriteOnly));
        QCOMPARE(library.write(original), original.size());
        library.close();
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_DATA_HOME", data);
        environment.insert("XDG_CONFIG_HOME", isolated.filePath("config"));
        environment.insert("XDG_CACHE_HOME", isolated.filePath("cache"));
        environment.insert("QT_QPA_PLATFORM", "offscreen");
        environment.insert("QT_QUICK_BACKEND", "software");
        QStringList arguments;
        if (mode == "screenshot")
            arguments = {"--new", "--screenshot", isolated.filePath("window.png")};
        else if (mode == "self-test")
            arguments = {"--self-test"};
        else {
            Document d;
            d.newProject();
            d.addShape("rect");
            const auto project = isolated.filePath("input.blazer");
            QVERIFY(d.save(QUrl::fromLocalFile(project)));
            arguments = {"--open-project", project, "--export-gcode", isolated.filePath("output.gcode")};
        }
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(BLAZER_APP), arguments);
        QVERIFY(process.waitForStarted(5000));
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        QCOMPARE(read(libraryPath), original);
        QVERIFY(!QFile::exists(libraryPath + ".before-manufacturer-v1.bak"));
    }
    void offlineModeCannotSelectHardware() {
        Document d;
        Machine machine(&d, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(machine.ports().isEmpty());
        QVERIFY(!machine.connectHardware("/must-not-be-opened-by-tests"));
        QVERIFY(machine.simulation());
        QVERIFY(machine.ready());
        QVERIFY(!machine.firing());
    }
    void onboardingPersistsOnlyAfterCompletion() {
        const auto ini = storage.filePath("onboarding.ini");
        Document d;
        Simulator m;
        {
            ApplicationServices service(&d, &m, ini);
            QVERIFY(service.firstRun());
        }
        {
            ApplicationServices service(&d, &m, ini);
            QVERIFY(service.firstRun());
            QVERIFY(service.finishOnboarding());
            QVERIFY(!service.firstRun());
        }
        ApplicationServices service(&d, &m, ini);
        QVERIFY(!service.firstRun());
        QVERIFY(service.version().startsWith("0.9."));
        QVERIFY(!service.revision().isEmpty());
    }
    void diagnosticsExcludeUserContentAndOnlyWriteLocalJson() {
        Document d;
        d.newProject();
        d.addShape("text");
        d.setProperty("text", "PRIVATE-TEXT-7ce8");
        const auto project = QUrl::fromLocalFile(storage.filePath("PRIVATE-PATH-7ce8.blazer"));
        QVERIFY(d.save(project));
        d.setBackgroundPlanning(true);
        Simulator m;
        ApplicationServices service(&d, &m, storage.filePath("diagnostics.ini"));
        const auto output = storage.filePath("diagnostics.json");
        QVERIFY(service.exportDiagnostics(QUrl::fromLocalFile(output)));
        const auto bytes = read(output);
        QVERIFY(!bytes.contains("PRIVATE"));
        QVERIFY(!bytes.contains(storage.path().toUtf8()));
        const auto report = QJsonDocument::fromJson(bytes).toVariant().toMap();
        QCOMPARE(report["schema"].toInt(), 1);
        QCOMPARE(report["hardwareAcceptance"].toString(), QString("pending"));
        QCOMPARE(report["externalUsabilityTest"].toString(), QString("pending"));
        QCOMPARE(report["deviceState"].toMap()["simulation"].toBool(), true);
        QCOMPARE(report["projectState"].toMap()["objects"].toInt(), 1);
        QVERIFY(!service.exportDiagnostics(QUrl("https://example.org/report.json")));
        QVERIFY(!service.exportDiagnostics(project));
        QCOMPARE(read(project.toLocalFile()), d.projectBytes());
        QVERIFY(
            !service.exportDiagnostics(QUrl::fromLocalFile(storage.filePath("missing/x.json"))));
        QVERIFY(service.exportDiagnostics(QUrl::fromLocalFile(output)));
        QCOMPARE(read(output + ".bak"), bytes);
    }
    void startupRecoveryIsKeptUntilExplicitDecision() {
        const auto directory = storage.filePath("recovery-keep");
        Document d;
        {
            Recovery recovery(&d, directory);
            d.addShape("rect");
            QVERIFY(recovery.save());
        }
        const auto path = directory + "/autosave.blazer";
        const auto original = read(path);
        QVERIFY(!original.isEmpty());
        {
            Document next;
            Recovery recovery(&next, directory);
            QVERIFY(recovery.available());
            recovery.closeSession();
        }
        QCOMPARE(read(path), original);
        {
            Document next;
            Recovery recovery(&next, directory);
            QVERIFY(recovery.restore());
            QVERIFY(next.dirty());
            recovery.closeSession();
        }
        QVERIFY(!QFile::exists(path));
        {
            Document next;
            Recovery recovery(&next, directory);
            next.addShape("rect");
            QVERIFY(recovery.save());
            recovery.closeSession();
            QVERIFY(!recovery.available());
        }
    }
    void cappedMaterialFixtureNeverExceedsFivePercent() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 97.5);
        d.setProperty("y", 72.5);
        d.setProperty("width", 5.);
        d.setProperty("height", 5.);
        d.setLayerSetting(d.items()[0].layer, "mode", "Schnitt");
        d.setLayerSetting(d.items()[0].layer, "power", 5.);
        d.setLayerSetting(d.items()[0].layer, "speed", 5000.);
        d.setLayerSetting(d.items()[0].layer, "passes", 1);
        const auto plan = d.job();
        QVERIFY2(plan.valid(), qPrintable(plan.errors.join("; ")));
        for (const auto &move : plan.moves) {
            QVERIFY(move.power >= 0 && move.power <= 5.);
            if (move.rapid) {
                QCOMPARE(move.power, 0.);
                continue;
            }
            QVERIFY(move.from.x() >= 50 && move.from.x() <= 150);
            QVERIFY(move.to.x() >= 50 && move.to.x() <= 150);
            QVERIFY(move.from.y() >= 25 && move.from.y() <= 125);
            QVERIFY(move.to.y() >= 25 && move.to.y() <= 125);
        }
        const QRegularExpression powerWord(R"((?:^|\s)S([0-9]+(?:\.[0-9]+)?)(?:\s|$))");
        for (const auto &line : plan.gcode()) {
            const auto match = powerWord.match(line);
            if (match.hasMatch())
                QVERIFY(match.captured(1).toDouble() <= 50.); // Smax=1000 => at most 5%.
        }
        const auto output = qEnvironmentVariable("BLAZER_ACCEPTANCE_FIXTURE_DIR");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(d.save(QUrl::fromLocalFile(output + "/kraftpapier-5pct.blazer")));
            QVERIFY(d.exportGcode(QUrl::fromLocalFile(output + "/kraftpapier-5pct.gcode")));
        }
    }
};
QTEST_MAIN(ReleaseTest)
#include "release_test.moc"
