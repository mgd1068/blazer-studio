#include "document.h"
#include "nativedesign.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
class NativeCompatibilityTest : public QObject {
    Q_OBJECT
    QByteArray rect =
        "<Shape Type='Rect' W='10' H='5' CutIndex='2'><XForm>1 0 0 1 20 20</XForm></Shape>";
    QByteArray project(QByteArray shapes = {}, QByteArray settings = {}) {
        return "<LightBurnProject FormatVersion='1'>" + settings +
               (shapes.isEmpty() ? rect : shapes) + "</LightBurnProject>";
    }
    NativeDesign::ReadOptions geometry(bool fonts = false) {
        return {NativeDesign::ImportIntent::GeometryOnly, fonts};
    }
    bool issue(const NativeDesign::CompatibilityReport &report, const QString &code) {
        return std::any_of(report.issues.begin(), report.issues.end(),
                           [&](const auto &value) { return value.code == code; });
    }
  private slots:
    void strictDefaultNeverSilentlyMigratesExecutionSemantics() {
        const auto bytes =
            project({}, "<CutSetting type='Cut'><index Value='2'/><maxPower Value='80'/><speed "
                        "Value='20'/><speedUnits Value='mm/sec'/><numPasses "
                        "Value='4'/><SubOperations><SubOperation/></SubOperations><AirAssist "
                        "Value='1'/><doOutput Value='0'/><Kerf Value='.2'/></CutSetting>");
        const auto strict = NativeDesign::read(bytes, false);
        QVERIFY(!strict.error.isEmpty());
        QVERIFY(strict.elements.isEmpty());
        QVERIFY(strict.compatibility.blocked);
        QVERIFY(issue(strict.compatibility, "semantic-migration-unavailable"));
        QVERIFY(issue(strict.compatibility, "power-not-migrated"));
        QVERIFY(issue(strict.compatibility, "feed-units-not-migrated"));
        QVERIFY(issue(strict.compatibility, "passes-suboperations-not-migrated"));
        QVERIFY(issue(strict.compatibility, "auxiliary-output-not-migrated"));
        QVERIFY(issue(strict.compatibility, "source-disabled-output-not-preserved"));
        QVERIFY(issue(strict.compatibility, "offsets-not-migrated"));
        QCOMPARE(strict.compatibility.sourceSha256,
                 QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const auto explicitGeometry = NativeDesign::read(bytes, false, geometry());
        QVERIFY2(explicitGeometry.error.isEmpty(), qPrintable(explicitGeometry.error));
        QCOMPARE(explicitGeometry.elements.size(), 1);
        QVERIFY(explicitGeometry.compatibility.canImportGeometry);
        QVERIFY(explicitGeometry.compatibility.geometryValidated);
        QVERIFY(
            !explicitGeometry.compatibility.toVariantMap()["semanticMigrationSupported"].toBool());
    }
    void textSubstitutionRequiresSeparateExplicitPermissionAndEmptyBackupDoesNotBypassIt() {
        for (const QByteArray &backup : {QByteArray{}, QByteArray("<BackupPath/>")}) {
            const auto bytes = project("<Shape Type='Text' Str='Hello' Font='DejaVu Sans' H='5'>" +
                                       backup + "</Shape>");
            auto denied = NativeDesign::read(bytes, false, geometry());
            QVERIFY(!denied.error.isEmpty());
            QVERIFY(issue(denied.compatibility, "font-substitution-required"));
            const auto allowed = NativeDesign::read(bytes, false, geometry(true));
            QVERIFY2(allowed.error.isEmpty(), qPrintable(allowed.error));
            QCOMPARE(allowed.elements.size(), 1);
        }
        const auto outlined =
            project("<Shape Type='Text' Str='Unused' Font='Unavailable font'><BackupPath>" + rect +
                    "</BackupPath></Shape>");
        const auto result = NativeDesign::read(outlined, false, geometry());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!issue(result.compatibility, "font-substitution-required"));
    }
    void unsupportedGeometryMirrorsUnitsAndExternalResourcesCannotBeOptedAway() {
        const QList<QByteArray> invalid = {
            project("<Shape Type='Rect' W='10' H='5' Rotation='90'/>"),
            QByteArray("<LightBurnProject MirrorX='True'>") + rect + "</LightBurnProject>",
            QByteArray("<LightBurnProject Units='in'>") + rect + "</LightBurnProject>",
            project("<Shape Type='Bitmap' W='10' H='5' File='/never/read.png'/>"),
            project({}, "<Script href='file:///never/read.js'/>"),
            project(
                "<Shape Type='Rect' W='10' H='5'><XForm Invert='true'>1 0 0 1 0 0</XForm></Shape>"),
            project({}, "<UnknownGeometryContainer/>")};
        for (const auto &bytes : invalid) {
            const auto result = NativeDesign::read(bytes, false, geometry(true));
            QVERIFY2(!result.error.isEmpty(), bytes.constData());
            QVERIFY(result.elements.isEmpty());
            QVERIFY(!result.compatibility.canImportGeometry);
        }
    }
    void reportsNeverEchoRawSourceValuesPathsOrNamesAndAggregateBoundedly() {
        QByteArray settings = "<Operation><feedRate Value='PRIVATE_FEED'/><CustomGCode "
                              "Value='PRIVATE_SCRIPT'/></Operation><Notes>PRIVATE_NOTE</Notes>";
        QByteArray shapes;
        for (int i = 0; i < 1000; ++i)
            shapes += rect;
        const auto bytes = project(shapes, settings);
        const auto report = NativeDesign::compatibility(bytes, false, geometry());
        QVERIFY(report.issues.size() < 128);
        const auto rendered = QJsonDocument::fromVariant(report.toVariantMap()).toJson();
        QVERIFY(!rendered.contains("PRIVATE_"));
        QVERIFY(!rendered.contains("/never/"));
        const auto assignment =
            std::find_if(report.issues.begin(), report.issues.end(), [](auto value) {
                return value.code == "operation-assignment-not-migrated";
            });
        QVERIFY(assignment != report.issues.end());
        QCOMPARE(assignment->count, 1000);
        auto changed = bytes;
        changed.replace("PRIVATE_FEED", "OTHER_FEED");
        QVERIFY(NativeDesign::compatibility(changed, false, geometry()).sourceSha256 !=
                report.sourceSha256);
    }
    void malformedGeometryCannotClaimValidatedCompatibilityAndInputsAreBounded() {
        const auto preflight = NativeDesign::compatibility(
            project("<Shape Type='Path'><VertList>bad</VertList><PrimList>L0 1</PrimList></Shape>"),
            false, geometry());
        QVERIFY(!preflight.geometryValidated);
        const auto parsed = NativeDesign::read(
            project("<Shape Type='Path'><VertList>bad</VertList><PrimList>L0 1</PrimList></Shape>"),
            false, geometry());
        QVERIFY(!parsed.compatibility.geometryValidated);
        QVERIFY(!parsed.compatibility.canImportGeometry);
        QVERIFY(issue(parsed.compatibility, "geometry-validation-failed"));
        const auto large =
            NativeDesign::compatibility(QByteArray(16 * 1024 * 1024 + 1, 'x'), false, geometry());
        QVERIFY(!large.complete);
        QVERIFY(!large.canImportGeometry);
        const auto dtd = NativeDesign::read(
            "<!DOCTYPE LightBurnProject [<!ENTITY source 'ignored'>]>" + project(), false,
            geometry());
        QVERIFY(!dtd.error.isEmpty());
    }
    void embeddedPngProvenanceNeverLoadsExternalSourceAndLegacyImportIsAtomic() {
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        const auto bytes = project(
            "<Shape Type='Bitmap' W='4' H='4' Data='" + png.toBase64() +
            "' File='/nonexistent/private-source.png'><XForm>1 0 0 1 10 10</XForm></Shape>");
        const auto result = NativeDesign::read(bytes, false, geometry());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.elements.first().image);
        QVERIFY(issue(result.compatibility, "external-reference-ignored"));
        QVERIFY(!QJsonDocument::fromVariant(result.compatibility.toVariantMap())
                     .toJson()
                     .contains("private-source"));
        QTemporaryDir directory;
        QFile file(directory.filePath("source.lbrn2"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(project());
        file.close();
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        const auto before = document.projectBytes();
        QVERIFY(!document.importFile(QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(document.projectBytes(), before);
    }
    void millmageOperationsAndToolLayersRemainGeometryOnly() {
        const auto bytes = QByteArray("<MillMageProject FormatVersion='1' MirrorX='False' "
                                      "MirrorY='False'><Operation><feedRate "
                                      "Value='500'/><spindleRPM Value='20000'/></Operation>") +
                           rect + "</MillMageProject>";
        QVERIFY(!NativeDesign::read(bytes, true).error.isEmpty());
        const auto result = NativeDesign::read(bytes, true, geometry());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(issue(result.compatibility, "cnc-operations-not-migrated"));
        const auto tools = NativeDesign::read(
            project("<Shape Type='Rect' W='10' H='5' CutIndex='30'/>"), false, geometry());
        QVERIFY(tools.error.isEmpty());
        QCOMPARE(tools.elements.first().toolLayer, 1);
    }
};
QTEST_MAIN(NativeCompatibilityTest)
#include "nativecompatibility_test.moc"
