#include "applicationservices.h"
#include "document.h"
#include "machine.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>
class TechnicalDiagnosticsTest : public QObject {
    Q_OBJECT
    QByteArray contents(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    QUrl sentinel(QTemporaryDir &directory) {
        const auto path = directory.filePath("target.json");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("keep previous") != 13)
            qFatal("fixture write failed");
        return QUrl::fromLocalFile(path);
    }
  private slots:
    void defaultReportKeepsExistingPrivacySchemaWithoutControllerContext() {
        QTemporaryDir directory;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.setMetadata("PRIVATE_PROJECT_NAME", "PRIVATE_PROJECT_NOTES");
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ApplicationServices service(&document, &machine, directory.filePath("settings.ini"));
        const auto old = service.diagnostics(), preview = service.previewTechnicalDiagnostics();
        const auto report = preview["report"].toMap();
        QCOMPARE(report.keys(), old.keys());
        QVERIFY(!report.contains("controllerContext"));
        QCOMPARE(report["projectState"], old["projectState"]);
        QCOMPARE(report["deviceState"], old["deviceState"]);
        QCOMPARE(QJsonDocument::fromJson(preview["json"].toString().toUtf8()).toVariant().toMap(),
                 report);
        const auto text = preview["json"].toString();
        QVERIFY(!text.contains("PRIVATE_PROJECT"));
        QVERIFY(!text.contains("logs"));
        QVERIFY(!text.contains("port"));
        QCOMPARE(preview["digest"].toString().size(), 64);
        QVERIFY(text.toUtf8().size() <= 64 * 1024);
        QVERIFY(!service.diagnostics().contains("controllerContext"));
    }
    void controllerContextIsExplicitAndSnapshotBytesStayFrozenAcrossChanges() {
        QTemporaryDir directory;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ApplicationServices service(&document, &machine, directory.filePath("settings.ini"));
        const auto preview = service.previewTechnicalDiagnostics(true);
        const auto context = preview["report"].toMap()["controllerContext"].toMap();
        QCOMPARE(context, machine.controllerDiagnosticSnapshot());
        QCOMPARE(context["controller"].toMap()["reasonCode"].toString(), QString("simulator"));
        QVERIFY(context["facade"].toMap()["simulation"].toBool());
        const auto frozen = preview["json"].toString().toUtf8();
        document.addShape("rect");
        machine.toggleConnection(); // Simulator only; hardware disabled.
        QVERIFY(service.diagnostics()["projectState"].toMap() !=
                preview["report"].toMap()["projectState"].toMap());
        QVERIFY(machine.controllerDiagnosticSnapshot() != context);
        const auto target = QUrl::fromLocalFile(directory.filePath("frozen.json"));
        QVERIFY(service.exportTechnicalDiagnostics(target, preview["digest"].toString()));
        QCOMPARE(contents(target.toLocalFile()), frozen);
        const auto legacy = QUrl::fromLocalFile(directory.filePath("legacy-default.json"));
        QVERIFY(service.exportDiagnostics(legacy));
        const auto legacyReport =
            QJsonDocument::fromJson(contents(legacy.toLocalFile())).toVariant().toMap();
        QVERIFY(!legacyReport.contains("controllerContext"));
        QCOMPARE(legacyReport["projectState"], service.diagnostics()["projectState"]);
        QVERIFY(service.exportTechnicalDiagnostics(target, preview["digest"].toString()));
        QCOMPARE(contents(target.toLocalFile()), frozen);
        const auto perms = QFile::permissions(target.toLocalFile());
        QVERIFY(
            !(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                       QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
        QVERIFY(!service.diagnostics().contains("controllerContext"));
    }
    void replacedIdenticalPreviewsAndClearInvalidateOldOpaqueTokens() {
        QTemporaryDir directory;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ApplicationServices service(&document, &machine, directory.filePath("settings.ini"));
        const auto target = sentinel(directory);
        const auto first = service.previewTechnicalDiagnostics(),
                   second = service.previewTechnicalDiagnostics();
        QVERIFY(first["digest"] != second["digest"]);
        QVERIFY(!service.exportTechnicalDiagnostics(target, first["digest"].toString()));
        QCOMPARE(contents(target.toLocalFile()), QByteArray("keep previous"));
        service.clearTechnicalDiagnosticsPreview();
        QVERIFY(!service.exportTechnicalDiagnostics(target, second["digest"].toString()));
        QCOMPARE(contents(target.toLocalFile()), QByteArray("keep previous"));
        const auto third = service.previewTechnicalDiagnostics();
        QVERIFY(third["digest"] != first["digest"]);
        QVERIFY(third["digest"] != second["digest"]);
        QVERIFY(service.exportTechnicalDiagnostics(target, third["digest"].toString()));
    }
    void missingMalformedAndNonlocalExportsFailWithoutChangingTarget() {
        QTemporaryDir directory;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ApplicationServices service(&document, &machine, directory.filePath("settings.ini"));
        const auto target = sentinel(directory);
        QVERIFY(!service.exportTechnicalDiagnostics(target, QString(64, 'a')));
        const auto preview = service.previewTechnicalDiagnostics(true);
        const auto digest = preview["digest"].toString();
        for (const auto &bad :
             QStringList{"", QString(64, 'A'), digest + '\n', QString(63, 'a'), QString(64, 'b')})
            QVERIFY(!service.exportTechnicalDiagnostics(target, bad));
        for (const auto &bad : QList<QUrl>{
                 QUrl("https://example.invalid/target.json"), QUrl("file://remote/target.json"),
                 QUrl::fromLocalFile(directory.filePath("target.txt")),
                 QUrl::fromLocalFile(directory.filePath("folder.json")),
                 QUrl(target.toString() + "?query"), QUrl(target.toString() + "#fragment")}) {
            if (bad.toLocalFile().endsWith("folder.json"))
                QVERIFY(QDir().mkdir(bad.toLocalFile()));
            QVERIFY(!service.exportTechnicalDiagnostics(bad, digest));
        }
        QCOMPARE(contents(target.toLocalFile()), QByteArray("keep previous"));
        const auto linked = directory.filePath("linked.json");
        QVERIFY(QFile::link(target.toLocalFile(), linked));
        QVERIFY(!service.exportTechnicalDiagnostics(QUrl::fromLocalFile(linked), digest));
        QCOMPARE(contents(target.toLocalFile()), QByteArray("keep previous"));
        QLockFile locked(target.toLocalFile() + ".lock");
        QVERIFY(locked.tryLock());
        QVERIFY(!service.exportTechnicalDiagnostics(target, digest));
        QCOMPARE(contents(target.toLocalFile()), QByteArray("keep previous"));
    }
    void qmlMethodsExistAndSimulatorOnlyFacadeCannotInventControllerTrace() {
        QTemporaryDir directory;
        Simulator simulator;
        ApplicationServices service(nullptr, &simulator, directory.filePath("settings.ini"));
        QVERIFY(service.metaObject()->indexOfMethod("previewTechnicalDiagnostics(bool)") >= 0);
        QVERIFY(service.metaObject()->indexOfMethod("exportTechnicalDiagnostics(QUrl,QString)") >=
                0);
        QVERIFY(service.metaObject()->indexOfMethod("clearTechnicalDiagnosticsPreview()") >= 0);
        QVERIFY(!service.previewTechnicalDiagnostics(true)["report"].toMap().contains(
            "controllerContext"));
    }
};
QTEST_MAIN(TechnicalDiagnosticsTest)
#include "technicaldiagnostics_test.moc"
