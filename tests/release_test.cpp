#include "applicationservices.h"
#include "machine.h"
#include "materiallibrary.h"
#include "recovery.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
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
    void lengthUnitPreferenceIsValidatedPersistentAndIndependent() {
        QTemporaryDir dir;
        const auto ini = dir.filePath("units.ini");
        Document d;
        d.newProject();
        d.addShape("rect");
        Simulator machine;
        const auto before = d.projectBytes();
        const auto revision = d.historyRevision();
        const auto code = d.job().gcode();
        {
            ApplicationServices service(&d, &machine, ini);
            QCOMPARE(service.lengthUnit(), QString("mm"));
            QVERIFY(!service.setLengthUnit("cm"));
            QVERIFY(service.setLengthUnit("in"));
            QCOMPARE(service.lengthUnit(), QString("in"));
            QCOMPARE(d.projectBytes(), before);
            QCOMPARE(d.historyRevision(), revision);
            QCOMPARE(d.job().gcode(), code);
        }
        {
            ApplicationServices restored(&d, &machine, ini);
            QCOMPARE(restored.lengthUnit(), QString("in"));
            QVERIFY(restored.setLengthUnit("mm"));
        }
        ApplicationServices service(&d, &machine, ini);
        QCOMPARE(service.lengthUnit(), QString("mm"));
        QVERIFY(QFile::remove(ini));
        QVERIFY(QDir().mkdir(ini));
        QVERIFY(!service.setLengthUnit("in"));
        QCOMPARE(service.lengthUnit(), QString("mm"));
        QVERIFY(service.message().contains("Maßeinheit"));
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(QDir().rmdir(ini));
        QVERIFY(service.setLengthUnit("in"));
        QCOMPARE(service.lengthUnit(), QString("in"));
        QSettings untrusted(dir.filePath("invalid.ini"), QSettings::IniFormat);
        untrusted.setValue("editor/v1/lengthUnit", "furlong");
        untrusted.sync();
        ApplicationServices invalid(&d, &machine, untrusted.fileName());
        QCOMPARE(invalid.lengthUnit(), QString("mm"));
        QVERIFY(invalid.setLengthUnit("mm"));
        untrusted.sync();
        QCOMPARE(untrusted.value("editor/v1/lengthUnit").toString(), QString("mm"));
    }
    void retiredSaveDialogReclaimsOnlyItsChooserResources() {
        Document d;
        Simulator machine;
        ApplicationServices service(&d, &machine, storage.filePath("dialog-ownership.ini"));
        QObject owner;
        auto *dialog = new QObject(&owner);
        dialog->setObjectName("saveProjectDialog");
        dialog->setProperty("title", "Project save");
        dialog->setProperty("visible", true);
        QPointer<QObject> chooser = new QObject(&owner);
        chooser->setProperty("title", "Project save");
        chooser->setProperty("selectedFile", QUrl());
        chooser->setProperty("currentFolder", QUrl());
        QObject unrelated(&owner);
        unrelated.setProperty("title", "Other chooser");
        unrelated.setProperty("selectedFile", QUrl());
        unrelated.setProperty("currentFolder", QUrl());
        QObject sameTitle(&owner);
        sameTitle.setProperty("title", "Project save");
        QCOMPARE(service.reclaimFileDialogResources(dialog, &owner), 0);
        dialog->setProperty("visible", false);
        QCOMPARE(service.reclaimFileDialogResources(nullptr, &owner), 0);
        QCOMPARE(service.reclaimFileDialogResources(dialog, nullptr), 0);
        QCOMPARE(service.reclaimFileDialogResources(dialog, &unrelated), 0);
        QCOMPARE(service.reclaimFileDialogResources(dialog, &owner), 1);
        QCOMPARE(chooser->parent(), dialog);
        QCOMPARE(unrelated.parent(), &owner);
        QCOMPARE(sameTitle.parent(), &owner);
        delete dialog;
        QVERIFY(chooser.isNull());
    }
    void recentProjectsPersistBoundAndDeduplicate() {
        QTemporaryDir dir;
        const auto ini = dir.filePath("settings.ini");
        Document d;
        Simulator machine;
        QList<QUrl> urls;
        {
            ApplicationServices service(&d, &machine, ini);
            QVERIFY(service.finishOnboarding());
            QVERIFY(service.recentProjects().isEmpty());
            for (int i = 0; i < 12; ++i) {
                const auto file =
                    QUrl::fromLocalFile(dir.filePath(QString("project %1.blazer").arg(i)));
                urls.append(file);
                QVERIFY(d.save(file));
            }
            QCOMPARE(service.recentProjects().size(), 10);
            QCOMPARE(service.recentProjects().first().toMap()["url"].toUrl(), urls[11]);
            QCOMPARE(service.recentProjects().last().toMap()["url"].toUrl(), urls[2]);
            QVERIFY(d.open(urls[5]));
            QCOMPARE(service.recentProjects().size(), 10);
            QCOMPARE(service.recentProjects().first().toMap()["url"].toUrl(), urls[5]);
            const auto alias = dir.filePath("alias.blazer");
            QVERIFY(QFile::link(urls[5].toLocalFile(), alias));
            QVERIFY(d.open(QUrl::fromLocalFile(alias)));
            QCOMPARE(service.recentProjects().size(), 10);
            QCOMPARE(service.recentProjects().first().toMap()["url"].toUrl(), urls[5]);
            const auto before = service.recentProjects();
            QVERIFY(!d.open(QUrl("https://example.org/project.blazer")));
            QVERIFY(!d.save(QUrl::fromLocalFile(dir.filePath("missing/project.blazer"))));
            QVERIFY(d.exportGcode(QUrl::fromLocalFile(dir.filePath("output.gcode"))));
            QCOMPARE(service.recentProjects(), before);
            QVERIFY(d.setMetadata("Unsaved", "Keep this"));
            const auto bytes = d.projectBytes();
            QVERIFY(!service.openRecentProject(urls[0])); // Evicted entry cannot be opened.
            QCOMPARE(d.projectBytes(), bytes);
        }
        ApplicationServices restored(&d, &machine, ini);
        QVERIFY(!restored.firstRun());
        QCOMPARE(restored.recentProjects().size(), 10);
        QCOMPARE(restored.recentProjects().first().toMap()["url"].toUrl(), urls[5]);
        const auto bytes = d.projectBytes();
        QVERIFY(restored.clearRecentProjects());
        QVERIFY(restored.recentProjects().isEmpty());
        QCOMPARE(d.projectBytes(), bytes);
        for (const auto &url : urls)
            QVERIFY(QFile::exists(url.toLocalFile()));
        ApplicationServices cleared(&d, &machine, ini);
        QVERIFY(cleared.recentProjects().isEmpty());
        QVERIFY(!cleared.firstRun());
    }
    void recentProjectsRejectUntrustedAndMissingFilesWithoutLosingCurrentProject() {
        QTemporaryDir dir;
        Document d;
        Simulator machine;
        const auto existing = QUrl::fromLocalFile(dir.filePath("valid #1.blazer"));
        const auto missing = QUrl::fromLocalFile(dir.filePath("missing.blazer"));
        QVERIFY(d.save(existing));
        const auto bytes = d.projectBytes();
        const auto ini = dir.filePath("settings.ini");
        const QStringList stored{missing.toString(QUrl::FullyEncoded),
                                 "https://example.org/project.blazer",
                                 "file:relative.blazer",
                                 "file://other-computer/project.blazer",
                                 existing.toString(QUrl::FullyEncoded),
                                 existing.toString(QUrl::FullyEncoded)};
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue("projects/v1/recent", stored);
            settings.sync();
        }
        ApplicationServices service(&d, &machine, ini);
        QCOMPARE(service.recentProjects().size(), 2);
        QVERIFY(!service.recentProjects().first().toMap()["available"].toBool());
        QVERIFY(service.recentProjects().last().toMap()["available"].toBool());
        QVERIFY(!service.openRecentProject(missing));
        QCOMPARE(d.projectBytes(), bytes);
        QCOMPARE(service.recentProjects().size(), 2);
        {
            QSettings settings(ini, QSettings::IniFormat);
            QCOMPARE(settings.value("projects/v1/recent").toStringList(), stored);
        }
        // A file may still exist while its contents have become corrupt.
        QFile file(existing.toLocalFile());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("broken project") > 0);
        file.close();
        QVERIFY(!service.openRecentProject(existing));
        QCOMPARE(d.projectBytes(), bytes);
        QCOMPARE(service.recentProjects().first().toMap()["url"].toUrl(), missing);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QVERIFY(service.openRecentProject(existing));
        QCOMPARE(service.recentProjects().first().toMap()["url"].toUrl(), existing);
        QVERIFY(QFile::copy(existing.toLocalFile(), missing.toLocalFile()));
        service.refreshRecentProjects();
        QVERIFY(service.recentProjects().last().toMap()["available"].toBool());
    }
    void recentProjectSettingsFailureDoesNotFailProjectSaveOrClaimClearedHistory() {
        QTemporaryDir dir;
        Document d;
        Simulator machine;
        const auto ini = dir.filePath("settings.ini");
        ApplicationServices service(&d, &machine, ini);
        const auto url = QUrl::fromLocalFile(dir.filePath("project.blazer"));
        QVERIFY(d.save(url));
        QCOMPARE(service.recentProjects().size(), 1);
        QVERIFY(QFile::remove(ini));
        QVERIFY(QDir().mkdir(ini)); // Reproducible write failure, including when tests run as root.
        QVERIFY(!service.clearRecentProjects());
        QCOMPARE(service.recentProjects().size(), 1);
        QVERIFY(d.setMetadata("Changed", "Project data is more important than history"));
        QVERIFY(d.save(url));
        QVERIFY(!d.dirty());
        QCOMPARE(read(url.toLocalFile()), d.projectBytes());
        QVERIFY(service.message().contains("Dateiverlauf"));
        QCOMPARE(service.recentProjects().size(), 1);
        QVERIFY(QDir().rmdir(ini));
        QVERIFY(service.clearRecentProjects()); // Recover after the settings location is repaired.
        QVERIFY(service.recentProjects().isEmpty());
        QVERIFY(d.save(url));
        QCOMPARE(service.recentProjects().size(), 1);
        QVERIFY(service.message().isEmpty());
    }
    void offlineCliLeavesUserLibraryUntouched_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("screenshot") << QString("screenshot");
        QTest::newRow("gcode-export") << QString("export");
        QTest::newRow("template-export") << QString("template");
        QTest::newRow("self-test") << QString("self-test");
    }
    void offlineCliLeavesUserLibraryUntouched() {
        QFETCH(QString, mode);
        QTemporaryDir isolated;
        const auto data = isolated.filePath("data");
        const auto libraryDirectory = data + "/BlazerStudio/Blazer Studio";
        QVERIFY(QDir().mkpath(libraryDirectory));
        const auto libraryPath = libraryDirectory + "/materials.json";
        const auto configDirectory = isolated.filePath("config/BlazerStudio");
        QVERIFY(QDir().mkpath(configDirectory));
        const auto settingsPath = configDirectory + "/Blazer Studio.conf";
        const QByteArray originalSettings =
            "[projects]\nv1\\recent=file:///private/original.blazer\n";
        QFile settings(settingsPath);
        QVERIFY(settings.open(QIODevice::WriteOnly));
        QCOMPARE(settings.write(originalSettings), originalSettings.size());
        settings.close();
        const auto original =
            QJsonDocument::fromVariant(
                QVariantMap{{"schemaVersion", 1},
                            {"profiles", QVariantList{MaterialLibrary::example()}}})
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
        QByteArray expectedGcode;
        if (mode == "screenshot")
            arguments = {"--new", "--screenshot", isolated.filePath("window.png")};
        else if (mode == "self-test")
            arguments = {"--self-test"};
        else {
            Document d;
            d.newProject();
            d.addShape("rect");
            const auto project =
                isolated.filePath(mode == "template" ? "input.blazertemplate" : "input.blazer");
            if (mode == "template")
                QVERIFY(d.saveTemplate(QUrl::fromLocalFile(project)));
            else
                QVERIFY(d.save(QUrl::fromLocalFile(project)));
            expectedGcode = (d.job().gcode().join('\n') + '\n').toUtf8();
            arguments = {mode == "template" ? "--new-from-template" : "--open-project", project,
                         "--export-gcode", isolated.filePath("output.gcode")};
        }
        QProcess process;
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(BLAZER_APP), arguments);
        QVERIFY(process.waitForStarted(5000));
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        if (!expectedGcode.isEmpty())
            QCOMPARE(read(isolated.filePath("output.gcode")), expectedGcode);
        QCOMPARE(read(libraryPath), original);
        QCOMPARE(read(settingsPath), originalSettings);
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
        QVERIFY(d.setMetadata("PRIVATE-TITLE-7ce8", "PRIVATE-NOTES-7ce8"));
        const auto project = QUrl::fromLocalFile(storage.filePath("PRIVATE-PATH-7ce8.blazer"));
        QVERIFY(d.save(project));
        d.setBackgroundPlanning(true);
        Simulator m;
        ApplicationServices service(&d, &m, storage.filePath("diagnostics.ini"));
        QVERIFY(d.save(project));
        QVERIFY(!service.recentProjects().isEmpty());
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
