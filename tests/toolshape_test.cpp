#include "document.h"
#include "nativedesign.h"
#include <QClipboard>
#include <QDomDocument>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTemporaryDir>
#include <QtTest>

class ToolShapeTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QUrl write(const QString &name, const QByteArray &bytes) {
        QFile f(temp.filePath(name));
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
            return {};
        return QUrl::fromLocalFile(f.fileName());
    }
  private slots:
    void helpersNeverEnterJobBoundsFrameOrSvgEvenOutsideBed() {
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto baseline = d.job().gcode();
        const auto bounds = d.activeBounds();
        d.addShape("rect");
        QVERIFY(d.setToolLayer(1));
        d.setProperty("x", -400.);
        d.setProperty("y", 350.);
        d.setProperty("width", 300.);
        d.setObjectSetting("power", 100.);
        d.assignOperations({1, 0});
        QCOMPARE(d.items()[1].toolLayer, 1);
        QCOMPARE(d.job().gcode(), baseline);
        QCOMPARE(d.activeBounds(), bounds);
        QVERIFY(d.issues().isEmpty());
        const auto svg = QUrl::fromLocalFile(temp.filePath("normal-only.svg"));
        QVERIFY(d.exportSvg(svg));
        QFile f(svg.toLocalFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QDomDocument doc;
        QVERIFY(doc.setContent(f.readAll()));
        QCOMPARE(doc.elementsByTagName("path").size(), 1);
        auto input = d.jobInput();
        input.items.removeLast();
        QCOMPARE(JobPlanner::build(d.jobInput()).gcode(), JobPlanner::build(input).gcode());
        d.select(0);
        QVERIFY(d.setToolLayer(2));
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().moves.isEmpty());
        QVERIFY(d.job().gcode().isEmpty());
        QVERIFY(!d.activeBounds()["valid"].toBool());
        QVERIFY(!d.exportGcode(QUrl::fromLocalFile(temp.filePath("no-output.gc"))));
        QVERIFY(!QFile::exists(temp.filePath("no-output.gc")));
        input = d.jobInput();
        input.items[0].toolLayer = 3;
        QVERIFY(!JobPlanner::build(input).valid());
    }
    void undoRoundtripAndExplicitOperationRestoreAreAtomic() {
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto normal = d.projectBytes();
        const auto code = d.job().gcode();
        const auto position = d.historyPosition();
        QVERIFY(d.setToolLayer(1));
        const auto helper = d.projectBytes();
        QCOMPARE(d.historyPosition(), position + 1);
        QCOMPARE(QJsonDocument::fromJson(helper).object()["version"].toInt(), 10);
        d.undo();
        QCOMPARE(d.projectBytes(), normal);
        d.redo();
        QCOMPARE(d.projectBytes(), helper);
        Document restored;
        QVERIFY(restored.restoreBytes(helper));
        QCOMPARE(restored.items()[0].toolLayer, 1);
        QVERIFY(!restored.outputs(restored.items()[0]));
        QVERIFY(!d.setToolLayer(3));
        QCOMPARE(d.projectBytes(), helper);
        const auto restorePosition = d.historyPosition();
        d.setLayer(d.items()[0].layer);
        QCOMPARE(d.historyPosition(), restorePosition + 1);
        QCOMPARE(d.items()[0].toolLayer, 0);
        QCOMPARE(d.job().gcode(), code);
        d.undo();
        QCOMPARE(d.projectBytes(), helper);
        d.addShape("ellipse");
        d.toggleSelection(0);
        QVERIFY(d.setToolLayer(2));
        QCOMPARE(d.selection()["toolLayer"].toInt(), 2);
        QCOMPARE(d.items()[0].toolLayer, 2);
        QCOMPARE(d.items()[1].toolLayer, 2);
        d.select(0);
        d.setLayer(1);
        d.toggleSelection(1);
        QCOMPARE(d.selection()["toolLayer"].toInt(), -1);
    }
    void malformedOrDowngradedHelperDataIsRejectedWithoutChanges() {
        Document d;
        d.newProject();
        d.addShape("rect");
        QVERIFY(d.setToolLayer(2));
        const auto before = d.projectBytes();
        auto root = QJsonDocument::fromJson(before).object();
        const auto object = root["objects"].toArray()[0].toObject();
        for (const QJsonValue &bad : QList<QJsonValue>{-1, 3, .5, "1", true, QJsonValue::Null}) {
            auto altered = object;
            altered["toolLayer"] = bad;
            root["objects"] = QJsonArray{altered};
            QVERIFY(!d.restoreBytes(QJsonDocument(root).toJson()));
            QCOMPARE(d.projectBytes(), before);
        }
        auto missing = object;
        missing.remove("toolLayer");
        root["objects"] = QJsonArray{missing};
        QVERIFY(!d.restoreBytes(QJsonDocument(root).toJson()));
        root["objects"] = QJsonArray{object};
        root["version"] = 9;
        QVERIFY(!d.restoreBytes(QJsonDocument(root).toJson()));
        QCOMPARE(d.projectBytes(), before);
        Document empty;
        empty.newProject();
        empty.addShape("rect");
        QCOMPARE(QJsonDocument::fromJson(empty.projectBytes()).object()["version"].toInt(), 8);
        empty.addGuide("vertical", 30.);
        QCOMPARE(QJsonDocument::fromJson(empty.projectBytes()).object()["version"].toInt(), 9);
    }
    void clipboardPagesTemplatesAndImportKeepNonOutputRole() {
        Document source;
        source.newProject();
        source.addShape("text");
        QVERIFY(source.setToolLayer(2));
        QVERIFY(source.copySelectionToClipboard());
        const auto *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasFormat("application/x-blazer-objects-v1"));
        QVERIFY(!mime->hasFormat("image/svg+xml"));
        QVERIFY(!mime->hasText());
        QVERIFY(!mime->hasImage());
        Document dest;
        dest.newProject();
        QVERIFY(dest.pasteFromClipboard());
        QCOMPARE(dest.items()[0].toolLayer, 2);
        QVERIFY(!dest.job().valid());
        QVERIFY(source.copyPage(0, "Kopie"));
        QCOMPARE(source.items()[1].toolLayer, 2);
        QVERIFY(source.items()[0].id != source.items()[1].id);
        const auto path = write("helpers.blazer", source.projectBytes());
        QVERIFY(dest.importProject(path));
        QCOMPARE(dest.count(), 3);
        for (const auto &a : dest.items()) {
            QCOMPARE(a.toolLayer, 2);
            QVERIFY(!dest.outputs(a));
        }
        const auto templatePath = QUrl::fromLocalFile(temp.filePath("helpers.blazertemplate"));
        QVERIFY(source.saveTemplate(templatePath));
        Document fresh;
        QVERIFY(fresh.newFromTemplate(templatePath));
        QCOMPARE(fresh.items().size(), 2);
        for (const auto &a : fresh.items())
            QCOMPARE(a.toolLayer, 2);
        source.select(0);
        source.toggleSelection(1);
        QVERIFY(source.copySelectionToClipboard(true));
        QCOMPARE(source.count(), 0);
        source.undo();
        QCOMPARE(source.count(), 2);
        for (const auto &a : source.items())
            QCOMPARE(a.toolLayer, 2);
    }
    void imageAndPathHelpersStayEditableWithoutOutput() {
        QImage image(8, 6, QImage::Format_ARGB32);
        image.fill(Qt::black);
        const auto file = temp.filePath("helper.png");
        QVERIFY(image.save(file));
        Document bitmap;
        bitmap.newProject();
        QVERIFY(bitmap.importImage(QUrl::fromLocalFile(file)));
        QVERIFY(bitmap.setToolLayer(1));
        const auto beforeModeChange=bitmap.projectBytes();
        bitmap.setLayerSetting(0,"mode","Schnitt");
        QCOMPARE(bitmap.projectBytes(),beforeModeChange);
        Document modeRoundtrip;QVERIFY(modeRoundtrip.restoreBytes(bitmap.projectBytes()));
        bitmap.setProperty("x", -200.);
        QVERIFY(bitmap.job().moves.isEmpty());
        QVERIFY(bitmap.copySelectionToClipboard());
        const auto *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(!mime->hasImage());
        QVERIFY(!mime->hasFormat("image/png"));
        Document restored;
        restored.newProject();
        QVERIFY(restored.pasteFromClipboard(true));
        QCOMPARE(restored.count(),1);
        QCOMPARE(restored.items()[0].kind, QString("image"));
        QCOMPARE(restored.items()[0].toolLayer, 1);
        QVERIFY(restored.items()[0].reliefAsset);
        QVERIFY(restored.job().moves.isEmpty());
        Document vectors;
        vectors.newProject();
        vectors.addShape("rect");
        vectors.addShape("ellipse");
        vectors.toggleSelection(0);
        QVERIFY(vectors.setToolLayer(2));
        QVERIFY(vectors.command("union"));
        QCOMPARE(vectors.count(), 1);
        QCOMPARE(vectors.items()[0].kind, QString("path"));
        QCOMPARE(vectors.items()[0].toolLayer, 2);
        QVERIFY(vectors.command("rotate", {{"value", 15.}}));
        QVERIFY(vectors.command("mirrorX"));
        QCOMPARE(vectors.items()[0].toolLayer, 2);
        QVERIFY(vectors.job().moves.isEmpty());
    }
    void nativeLightBurnToolIndicesAndTextOutlineRoleArePreserved() {
        const QByteArray shapes =
            "<Shape Type=\"Rect\" CutIndex=\"30\" W=\"20\" H=\"10\"><XForm>1 0 0 1 50 "
            "50</XForm></Shape>"
            "<Shape Type=\"Group\" CutIndex=\"31\"><Children>"
            "<Shape Type=\"Rect\" W=\"10\" H=\"10\"><XForm>1 0 0 1 80 50</XForm></Shape>"
            "<Shape Type=\"Rect\" CutIndex=\"0\" W=\"10\" H=\"10\"><XForm>1 0 0 1 100 "
            "50</XForm></Shape>"
            "</Children></Shape>"
            "<Shape Type=\"Text\" CutIndex=\"30\"><BackupPath>"
            "<Shape Type=\"Rect\" CutIndex=\"0\" W=\"5\" H=\"5\"><XForm>1 0 0 1 130 "
            "50</XForm></Shape>"
            "</BackupPath></Shape>";
        const auto file = "<LightBurnProject>" + shapes + "</LightBurnProject>";
        const auto parsed = NativeDesign::read(file, false);
        QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
        QCOMPARE(parsed.elements.size(), 4);
        const QList<int> expected{1, 2, 0, 1};
        for (int i = 0; i < 4; ++i)
            QCOMPARE(parsed.elements[i].toolLayer, expected[i]);
        Document d;
        d.newProject();
        QVERIFY(d.importFile(write("tools.lbrn2", file)));
        for (int i = 0; i < 4; ++i)
            QCOMPARE(d.items()[i].toolLayer, expected[i]);
        QVERIFY(d.job().valid());
        auto normal = d.jobInput();
        normal.items = {d.items()[2]};
        QCOMPARE(d.job().gcode(), JobPlanner::build(normal).gcode());
        const auto custom = NativeDesign::read(
            "<LightBurnProject><CutSetting type=\"Tool\"><index Value=\"5\"/></CutSetting><Shape "
            "Type=\"Rect\" CutIndex=\"5\" W=\"5\" H=\"5\"/></LightBurnProject>",
            false);
        QVERIFY(custom.error.isEmpty());
        QCOMPARE(custom.elements[0].toolLayer, 1);
        QVERIFY(!NativeDesign::read("<LightBurnProject><Shape Type=\"Rect\" CutIndex=\"30.5\" "
                                    "W=\"5\" H=\"5\"/></LightBurnProject>",
                                    false)
                     .error.isEmpty());
    }
};
QTEST_MAIN(ToolShapeTest)
#include "toolshape_test.moc"
