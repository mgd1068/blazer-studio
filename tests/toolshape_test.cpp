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
    void frameTargetsSeparateVisibleHelpersSelectionAndPlannedOutput() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 10.);
        d.setProperty("y", 20.);
        d.setProperty("width", 30.);
        d.setProperty("height", 10.);
        d.addShape("rect");
        d.setToolLayer(1);
        d.setProperty("x", 50.);
        d.setProperty("y", 60.);
        d.setProperty("width", 20.);
        d.setProperty("height", 15.);
        d.addShape("rect");
        d.setToolLayer(2);
        d.setProperty("x", 90.);
        d.setProperty("y", 100.);
        d.setProperty("width", 10.);
        d.setProperty("height", 10.);
        const auto bytes = d.projectBytes();
        const auto revision = d.historyRevision();
        const auto code = d.job().gcode();
        auto check = [](const QVariantMap &b, QRectF expected) {
            return b["valid"].toBool() &&
                   QRectF(b["x"].toDouble(), b["y"].toDouble(), b["width"].toDouble(),
                          b["height"].toDouble()) == expected;
        };
        QVERIFY(check(d.framingBounds("job"), {10, 20, 30, 10}));
        QVERIFY(check(d.framingBounds("t1"), {50, 60, 20, 15}));
        QVERIFY(check(d.framingBounds("t2"), {90, 100, 10, 10}));
        QVERIFY(check(d.framingBounds("tools"), {50, 60, 50, 50}));
        QVERIFY(check(d.framingBounds("selection"), {90, 100, 10, 10}));
        QVERIFY(!d.framingBounds("unknown")["valid"].toBool());
        QCOMPARE(d.projectBytes(), bytes);
        QCOMPARE(d.historyRevision(), revision);
        QCOMPARE(d.job().gcode(), code);
        d.toggleVisible(1);
        QVERIFY(!d.framingBounds("t1")["valid"].toBool());
        QVERIFY(check(d.framingBounds("tools"), {90, 100, 10, 10}));
        d.select(2);
        d.setProperty("x", 199.);
        QVERIFY(!d.framingBounds("t2")["valid"].toBool());
        QVERIFY(d.framingBounds("job")["valid"].toBool());
        QCOMPARE(d.job().gcode(), code);
        QVERIFY(d.addPage("Zwei"));
        d.activatePage(1);
        d.addShape("rect");
        d.setToolLayer(1);
        QVERIFY(d.framingBounds("t1")["valid"].toBool());
        QVERIFY(!d.framingBounds("t2")["valid"].toBool());
        d.activatePage(0);
        QVERIFY(!d.framingBounds("t1")["valid"].toBool());
        d.select(-1);
        d.toggleSelection(3);
        QVERIFY(!d.framingBounds("selection")["valid"].toBool());
    }
    void jobFrameIncludesOverscanAndHandlesStraightBedEdgePaths() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setLayer(0);
        d.setProperty("x", 20.);
        d.setProperty("y", 30.);
        d.setProperty("width", 40.);
        d.setProperty("height", 10.);
        d.setLayerSetting(0, "interval", 1.);
        d.setLayerSetting(0, "overscan", 2.);
        QVERIFY(d.job().valid());
        const auto region = d.framingBounds("job");
        QVERIFY(region["valid"].toBool());
        QCOMPARE(region["x"].toDouble(), 18.);
        QCOMPARE(region["width"].toDouble(), 44.);
        const QRectF frame(region["x"].toDouble(), region["y"].toDouble(),
                           region["width"].toDouble(), region["height"].toDouble());
        QVERIFY(!frame.contains(QPointF(0, 0)));
        for (const auto &move : d.job().moves)
            if (!move.rapid) {
                QVERIFY(frame.contains(move.from));
                QVERIFY(frame.contains(move.to));
            }
        Document line;
        line.newProject();
        QVERIFY(line.importFile(write("edge.gc", "G0 X0 Y0\nG1 X20 Y0\n")));
        QVERIFY(line.job().valid());
        const auto narrow = line.framingBounds("job");
        QVERIFY(narrow["valid"].toBool());
        QCOMPARE(narrow["x"].toDouble(), 0.);
        QCOMPARE(narrow["y"].toDouble(), 0.);
        QCOMPARE(narrow["width"].toDouble(), 20.);
        QCOMPARE(narrow["height"].toDouble(), .1);
    }
    void bedEdgePathBoxesPreservePhysicalEndpointsAndRejectOutsideGeometry() {
        const QList<QPair<QPointF,QPointF>> lines{{{10,0},{30,0}},{{10,150},{30,150}},{{0,10},{0,30}},{{200,10},{200,30}}};
        for(const auto &ends:lines) {
            const QByteArray vertices="V"+QByteArray::number(ends.first.x())+" "+QByteArray::number(ends.first.y())+"V"+QByteArray::number(ends.second.x())+" "+QByteArray::number(ends.second.y());
            const auto bytes=QByteArray(R"(<LightBurnProject><Shape Type="Path"><VertList>)")+vertices+"</VertList><PrimList>LineOpen</PrimList></Shape></LightBurnProject>";
            Document d;d.newProject();QVERIFY(d.importFile(write("edge.lbrn2",bytes)));QVERIFY(d.job().valid());
            QString error;const auto path=JobPlanner::geometry(d.items()[0],error,true);QVERIFY(error.isEmpty());
            QVERIFY(QLineF(QPointF(path.elementAt(0)),ends.first).length()<1e-9);
            QVERIFY(QLineF(QPointF(path.elementAt(1)),ends.second).length()<1e-9);
            const auto frame=d.framingBounds("job");QVERIFY(frame["valid"].toBool());
            const QRectF bounds(frame["x"].toDouble(),frame["y"].toDouble(),frame["width"].toDouble(),frame["height"].toDouble());
            QVERIFY(bounds.left()>=0 && bounds.top()>=0 && bounds.right()<=200 && bounds.bottom()<=150);
            QVERIFY(bounds.contains(ends.first));QVERIFY(bounds.contains(ends.second));
        }
        Document outside;outside.newProject();QVERIFY(outside.importFile(write("outside-edge.lbrn2",R"(<LightBurnProject><Shape Type="Path"><VertList>V-1 10V-1 30</VertList><PrimList>LineOpen</PrimList></Shape></LightBurnProject>)")));
        QVERIFY(!outside.job().valid());QVERIFY(!outside.framingBounds("job")["valid"].toBool());
        QVERIFY(!outside.framingBounds("selection")["valid"].toBool());
    }
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
