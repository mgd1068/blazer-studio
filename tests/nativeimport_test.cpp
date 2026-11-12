#include "document.h"
#include "nativedesign.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class NativeImportTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QUrl write(const QString &name, const QByteArray &data) {
        QFile file(temp.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
            return {};
        return QUrl::fromLocalFile(file.fileName());
    }
    QByteArray project(const QByteArray &shapes, const QByteArray &metadata = {}) {
        return "<MillMageProject FormatVersion=\"1\" MirrorX=\"False\" MirrorY=\"False\">" +
               metadata + shapes + "</MillMageProject>";
    }
    QByteArray png() {
        QImage image(2, 3, QImage::Format_ARGB32);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 2; ++x)
                image.setPixelColor(x, y,
                                    QColor((y * 2 + x) * 40, (y * 2 + x) * 40, (y * 2 + x) * 40));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    QByteArray bitmap(const QByteArray &transform = "1 0 0 1 10 10", const QByteArray &extra = {}) {
        return "<Shape Type=\"Bitmap\" W=\"4\" H=\"3\" Data=\"" + png().toBase64() + "\" " + extra +
               "><XForm>" + transform + "</XForm></Shape>";
    }
    QByteArray rectangle() {
        return "<Shape Type=\"Rect\" W=\"10\" H=\"5\"><XForm>1 0 0 1 20 20</XForm></Shape>";
    }
  private slots:
    void sharedLightBurnListsResolveForwardReferencesInLocalCoordinates() {
        const QByteArray data = R"(<LightBurnProject>
<Shape Type="Path" VertID="007" PrimID="9"><XForm>1 0 0 1 10 20</XForm></Shape>
<Shape Type="Group"><XForm>2 0 0 3 40 50</XForm><Children>
<Shape Type="Path" VertID="7" PrimID="9" CutIndex="30"><XForm>1 0 0 1 1 2</XForm><VertList>V0 0V6 0V6 4</VertList><PrimList>L0 1L1 2</PrimList></Shape>
</Children></Shape>
<Shape Type="Path" VertID="7" PrimID="8"><XForm>1 0 0 1 100 30</XForm><PrimList>L2 1L1 0</PrimList></Shape>
</LightBurnProject>)";
        const auto result = NativeDesign::read(data, false);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.elements.size(), 3);
        const QList<QList<QPointF>> expected{{{10, 20}, {16, 20}, {16, 24}},
                                             {{42, 56}, {54, 56}, {54, 68}},
                                             {{106, 34}, {106, 30}, {100, 30}}};
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(result.elements[i].path.elementCount(), 3);
            for (int j = 0; j < 3; ++j)
                QCOMPARE(QPointF(result.elements[i].path.elementAt(j)), expected[i][j]);
        }
        QCOMPARE(result.elements[0].toolLayer, 0);
        QCOMPARE(result.elements[1].toolLayer, 1);
        Document d;
        d.newProject();
        QVERIFY2(d.importFile(write("shared.lbrn2", data)), qPrintable(d.message()));
        QCOMPARE(d.count(), 3);
        QVERIFY(d.job().valid());
        const auto before = d.projectBytes();
        Document restored;
        QVERIFY(restored.restoreBytes(before));
        QCOMPARE(restored.job().gcode(), d.job().gcode());
        QCOMPARE(restored.items()[1].toolLayer, 1);
        d.undo();
        QCOMPARE(d.count(), 0);
        d.redo();
        QCOMPARE(d.projectBytes(), before);
    }
    void sharedCurvesAndIndependentPrimitiveListsKeepControls() {
        const QByteArray data = R"(<LightBurnProject>
<Shape Type="Path" VertID="0" PrimID="0"><XForm>1 0 0 1 10 20</XForm></Shape>
<Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0c0x2c0y3V6 4c1x5c1y1</VertList><PrimList>B0 1</PrimList></Shape>
<Shape Type="Path" VertID="0" PrimID="1"><PrimList>L0 1</PrimList></Shape>
</LightBurnProject>)";
        const auto result = NativeDesign::read(data, false);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.elements.size(), 3);
        const QList<QPointF> expected{{10, 20}, {12, 23}, {15, 21}, {16, 24}};
        QCOMPARE(result.elements[0].path.elementCount(), 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(QPointF(result.elements[0].path.elementAt(i)), expected[i]);
        QCOMPARE(result.elements[2].path.elementCount(), 2);
        QCOMPARE(QPointF(result.elements[2].path.elementAt(1)), QPointF(6, 4));
    }
    void ambiguousMissingAndMalformedSharedListsAreAtomic() {
        const QByteArray definition =
            R"(<Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0 V1 1</VertList><PrimList>L0 1</PrimList></Shape>)";
        auto document = [&](const QByteArray &extra) {
            return "<LightBurnProject>" + definition + extra + "</LightBurnProject>";
        };
        const QList<QByteArray> invalid{
            R"(<Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0 V1 2</VertList></Shape>)",
            R"(<Shape Type="Path" VertID="0" PrimID="0"><PrimList>L1 0</PrimList></Shape>)",
            R"(<Shape Type="Path" VertID="77" PrimID="0"/>)",
            R"(<Shape Type="Path" VertID="0" PrimID="77"/>)",
            R"(<Shape Type="Path" VertID="-1" PrimID="0"/>)",
            R"(<Shape Type="Path" VertID="1.5" PrimID="0"/>)",
            R"(<Shape Type="Path" VertID="1e0" PrimID="0"/>)",
            R"(<Shape Type="Path" VertID="" PrimID="0"/>)",
            R"(<Shape Type="Path" VertID="123456789012345678901234567890123" PrimID="0"/>)"};
        Document target;
        target.newProject();
        target.addShape("rect");
        const auto before = target.projectBytes();
        const auto revision = target.historyRevision();
        for (const auto &extra : invalid) {
            const auto result = NativeDesign::read(document(extra), false);
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.elements.isEmpty());
            QVERIFY(!target.importFile(write("bad-reference.lbrn2", document(extra))));
            QCOMPARE(target.projectBytes(), before);
            QCOMPARE(target.historyRevision(), revision);
        }
        const auto duplicate = NativeDesign::read(
            document(
                R"(<Shape Type="Path" VertID="00" PrimID="00"><VertList> V0 0  V1 1 </VertList><PrimList> L0 1 </PrimList></Shape>)"),
            false);
        QVERIFY2(duplicate.error.isEmpty(), qPrintable(duplicate.error));
        QCOMPARE(duplicate.elements.size(), 2);
    }
    void repeatedSharedGeometryStillCountsEveryInstanceAgainstBudget() {
        QByteArray vertices, primitives;
        for (int i = 0; i < 100; ++i) {
            vertices += "V" + QByteArray::number(i) + " " + QByteArray::number(i % 2);
            if (i)
                primitives += "L" + QByteArray::number(i - 1) + " " + QByteArray::number(i);
        }
        QByteArray data =
            R"(<LightBurnProject><Shape Type="Path" VertID="0" PrimID="0"><VertList>)" + vertices +
            "</VertList><PrimList>" + primitives + "</PrimList></Shape>";
        for (int i = 0; i < 1000; ++i)
            data += R"(<Shape Type="Path" VertID="0" PrimID="0"/>)";
        data += "</LightBurnProject>";
        const auto result = NativeDesign::read(data, false);
        QVERIFY(result.error.contains("Pfadpunkte"));
        QVERIFY(result.elements.isEmpty());
    }
    void millMageInlineIdsKeepTheirExistingInterpretation() {
        const auto data = project(
            R"(<Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0 V1 1</VertList><PrimList>L0 1</PrimList></Shape><Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0 V2 3</VertList><PrimList>L0 1</PrimList></Shape>)");
        const auto result = NativeDesign::read(data, true);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.elements.size(), 2);
        QCOMPARE(QPointF(result.elements[1].path.elementAt(1)), QPointF(2, 3));
    }
    void compactLineTopologyUsesEachInstancesVertexCount() {
        const QByteArray data = R"(<LightBurnProject>
<Shape Type="Path" VertID="0" PrimID="0"><VertList>V0 0V10 0V10 5V0 5</VertList><PrimList>LineClosed</PrimList></Shape>
<Shape Type="Path" VertID="1" PrimID="0"><VertList>V20 0V25 5V30 0</VertList></Shape>
<Shape Type="Path" VertID="2" PrimID="1"><VertList>V0 10V4 14V8 10</VertList><PrimList>LineOpen</PrimList></Shape>
</LightBurnProject>)";
        const auto result = NativeDesign::read(data, false);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.elements.size(), 3);
        QCOMPARE(result.elements[0].path.elementCount(), 5);
        QCOMPARE(result.elements[0].path.length(), 30.);
        QCOMPARE(result.elements[1].path.elementCount(), 4);
        QCOMPARE(QPointF(result.elements[1].path.elementAt(3)), QPointF(20, 0));
        QCOMPARE(result.elements[2].path.elementCount(), 3);
        QCOMPARE(QPointF(result.elements[2].path.elementAt(2)), QPointF(8, 10));
        const auto invalid = NativeDesign::read(
            R"(<LightBurnProject><Shape Type="Path"><VertList>V1 1</VertList><PrimList>LineClosed</PrimList></Shape></LightBurnProject>)",
            false);
        QVERIFY(!invalid.error.isEmpty());
        QVERIFY(invalid.elements.isEmpty());
    }
    void cubicC0LeavesStartAndC1EntersEndInBothNativeDialects() {
        const QByteArray shape =
            R"(<Shape Type="Path"><VertList>V0 0c0x2c0y3c1x-7c1y-8V6 4c0x18c0y19c1x5c1y1</VertList><PrimList>B0 1</PrimList></Shape>)";
        for (bool millmage : {false, true}) {
            const auto result = NativeDesign::read(
                millmage ? project(shape) : "<LightBurnProject>" + shape + "</LightBurnProject>",
                millmage);
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.elements.size(), 1);
            const QList<QPointF> expected{{0, 0}, {2, 3}, {5, 1}, {6, 4}};
            QCOMPARE(result.elements[0].path.elementCount(), 4);
            for (int i = 0; i < 4; ++i)
                QCOMPARE(QPointF(result.elements[0].path.elementAt(i)), expected[i]);
        }
    }
    void fallbackTextUsesLocalYUpAndKeepsExplicitParentReflection() {
        const QByteArray text=R"(<Shape Type="Text" Str="H" Font="DejaVu Sans" H="10"><XForm>1 0 0 1 20 30</XForm></Shape>)";
        const auto upright=NativeDesign::read("<LightBurnProject>"+text+"</LightBurnProject>",false);
        QVERIFY2(upright.error.isEmpty(),qPrintable(upright.error));QCOMPARE(upright.elements.size(),1);
        auto bounds=upright.elements[0].path.boundingRect();
        QVERIFY(qAbs(bounds.top()-30.)<1e-9);QVERIFY(qAbs(bounds.bottom()-40.)<1e-9);
        const auto reflected=NativeDesign::read(R"(<LightBurnProject><Shape Type="Group"><XForm>1 0 0 -1 0 100</XForm><Children>)"+text+"</Children></Shape></LightBurnProject>",false);
        QVERIFY2(reflected.error.isEmpty(),qPrintable(reflected.error));QCOMPARE(reflected.elements.size(),1);
        bounds=reflected.elements[0].path.boundingRect();
        QVERIFY(qAbs(bounds.top()-60.)<1e-9);QVERIFY(qAbs(bounds.bottom()-70.)<1e-9);
        QVERIFY(reflected.elements[0].group.isEmpty());
    }
    void vectorsGroupsVisibilityAndDisabledControls() {
        const auto data = project("<Shape Type=\"Group\"><XForm>2 0 0 3 10 20</XForm><Children>"
                                  "<Shape Type=\"Path\"><XForm>1 0 .5 1 2 4</XForm><VertList>V0 "
                                  "0V4 0V4 2</VertList><PrimList>L0 1L1 2</PrimList></Shape>"
                                  "<Shape Type=\"Rect\" W=\"6\" H=\"4\" LayerIndex=\"3\"><XForm>1 "
                                  "0 0 1 20 10</XForm></Shape>"
                                  "</Children></Shape>",
                                  "<Layer><index Value=\"3\"/><hide Value=\"1\"/></Layer>");
        const auto result = NativeDesign::read(data, true);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.elements.size(), 2);
        const auto &a = result.elements[0];
        QCOMPARE(QPointF(a.path.elementAt(0)), QPointF(14, 32));
        QCOMPARE(QPointF(a.path.elementAt(1)), QPointF(22, 32));
        QCOMPARE(QPointF(a.path.elementAt(2)), QPointF(24, 38));
        QVERIFY(a.visible);
        QVERIFY(!a.group.isEmpty());
        QCOMPARE(result.elements[1].group, a.group);
        QVERIFY(!result.elements[1].visible);
        QCOMPARE(result.elements[1].path.boundingRect(), QRectF(44, 44, 12, 12));
        const auto curve = NativeDesign::read(
            project(
                "<Shape Type=\"Path\"><XForm>1 0 0 1 5 6</XForm>"
                "<VertList>V0 0c1x3c1y0V10 5c0x10c0y2</VertList><PrimList>B0 1</PrimList></Shape>"),
            true);
        QVERIFY2(curve.error.isEmpty(), qPrintable(curve.error));
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(1)), QPointF(5, 6));
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(2)), QPointF(15, 11));
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(3)), QPointF(15, 11));
        const auto disabled =
            NativeDesign::read(project("<Shape Type=\"Path\"><VertList>V10 5c0x1V20 "
                                       "9c1x1</VertList><PrimList>B0 1</PrimList></Shape>"),
                               true);
        QVERIFY2(disabled.error.isEmpty(), qPrintable(disabled.error));
        QCOMPARE(QPointF(disabled.elements[0].path.elementAt(1)), QPointF(10, 5));
        QCOMPARE(QPointF(disabled.elements[0].path.elementAt(2)), QPointF(20, 9));
    }
    void embeddedImagesTransformWithoutExternalReads() {
        const auto straight = NativeDesign::read(project(bitmap()), true);
        QVERIFY2(straight.error.isEmpty(), qPrintable(straight.error));
        QCOMPARE(straight.elements[0].bounds, QRectF(8, 8.5, 4, 3));
        const auto &raw = straight.elements[0].image->image;
        QCOMPARE(raw.size(), QSize(2, 3));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(qGray(raw.pixel(x, y)), ((2 - y) * 2 + x) * 40);
        const auto reflected =
            NativeDesign::read(project(bitmap("-1 0 0 1 10 10", "File=\"/not/read.png\"")), true);
        QVERIFY2(reflected.error.isEmpty(), qPrintable(reflected.error));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(qGray(reflected.elements[0].image->image.pixel(x, y)),
                         qGray(raw.pixel(1 - x, y)));
        const auto rotated = NativeDesign::read(project(bitmap("0 1 -1 0 10 10")), true);
        QVERIFY2(rotated.error.isEmpty(), qPrintable(rotated.error));
        QCOMPARE(rotated.elements[0].bounds, QRectF(8.5, 8, 3, 4));
        QCOMPARE(rotated.elements[0].image->image.size(), QSize(3, 2));
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                QCOMPARE(qGray(rotated.elements[0].image->image.pixel(x, y)),
                         qGray(raw.pixel(1 - y, x)));
        const auto sheared = NativeDesign::read(project(bitmap("1 0 .5 1 10 10")), true);
        QVERIFY2(sheared.error.isEmpty(), qPrintable(sheared.error));
        QCOMPARE(sheared.elements[0].bounds, QRectF(7.25, 8.5, 5.5, 3));
        int clear = 0, ink = 0;
        const auto image = sheared.elements[0].image->image;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.pixelColor(x, y).alpha() == 0 ? ++clear : ++ink;
        QVERIFY(clear > 0 && ink > 0);
    }
    void nativeDocumentPreservesDestinationAndOwnParameters() {
        Document d;
        d.newProject();
        d.addShape("rect");
        QVERIFY(d.addPage("Ziel"));
        QVERIFY(d.setMetadata("Eigenes Projekt", "Private Notizen"));
        const auto own = QUrl::fromLocalFile(temp.filePath("own.blazer"));
        QVERIFY(d.save(own));
        const auto before = d.projectBytes();
        const auto layers = d.layers();
        const auto material = d.material();
        const auto source =
            project(rectangle() + bitmap(),
                    "<Operation><feedRate Value=\"33333\"/><spindleRPM Value=\"25000\"/>"
                    "<CustomGCode Value=\"M3S25000\"/></Operation><Notes>Foreign notes</Notes>");
        QVERIFY2(d.importFile(write("native.mage", source)), qPrintable(d.message()));
        QCOMPARE(d.count(), 3);
        QCOMPARE(d.items()[1].bounds, QRectF(15, 17.5, 10, 5));
        QCOMPARE(d.items()[2].kind, QString("image"));
        QCOMPARE(d.items()[2].layer, 0);
        QCOMPARE(d.items()[1].page, 1);
        QCOMPARE(d.layers(), layers);
        QCOMPARE(d.material(), material);
        QCOMPARE(d.title(), QString("Eigenes Projekt"));
        QCOMPARE(d.notes(), QString("Private Notizen"));
        QCOMPARE(d.fileUrl(), own);
        QVERIFY(d.items()[1].settings.isEmpty());
        QVERIFY(d.items()[2].settings.isEmpty());
        const auto imported = d.projectBytes();
        const auto code = d.job().gcode();
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QVERIFY(!code.join('\n').contains("S25000"));
        Document restored;
        QVERIFY(restored.restoreBytes(imported));
        QCOMPARE(restored.job().gcode(), code);
        QCOMPARE(restored.items()[2].reliefAsset->image, d.items()[2].reliefAsset->image);
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        QCOMPARE(d.fileUrl(), own);
        d.redo();
        QCOMPARE(d.projectBytes(), imported);
    }
    void malformedAndUnsupportedAreAtomic() {
        const QList<QByteArray> badShapes = {
            "<Shape Type=\"Unknown\"/>",
            "<Shape Type=\"Path\"><VertList>V0 0c1x2V1 1</VertList><PrimList>B0 "
            "1</PrimList></Shape>",
            "<Shape Type=\"Rect\" W=\"no\" H=\"5\"/>",
            "<Shape Type=\"Rect\" W=\"5\" H=\"-1\"/>",
            "<Shape Type=\"Rect\" W=\"5\" H=\"5\"><XForm>1 0 0 1 0 0 7</XForm></Shape>",
            "<Shape Type=\"Rect\" W=\"5\" H=\"5\"><XForm>1 0 0 1 0 0</XForm><XForm>1 0 0 1 9 "
            "9</XForm></Shape>",
            "<Shape Type=\"Path\"><VertList>V0 0V1 1</VertList><PrimList>L0 1 "
            "garbage</PrimList></Shape>",
            "<Shape Type=\"Path\"><VertList>V0 0V1 1 junk</VertList><PrimList>L0 "
            "1</PrimList></Shape>",
            "<Shape Type=\"Path\"><VertList>V0 0c1x1c1x2V1 1</VertList><PrimList>B0 "
            "1</PrimList></Shape>",
            "<Shape Type=\"Path\"><VertList>V0 0V1 1</VertList><PrimList>L0 2</PrimList></Shape>",
            "<Shape Type=\"Path\"><VertList>V0.10.2V1 1</VertList><PrimList>L0 "
            "1</PrimList></Shape>",
            "<Shape Type=\"Bitmap\" W=\"5\" H=\"5\" File=\"/not/read.png\"/>",
            bitmap("1 0 0 1 10 10", "Gamma=\"2\""),
            bitmap("1 0 0 1 10 10", "CropX=\"1\""),
            "<Shape Type=\"Text\" Str=\"No fallback\" H=\"5\"/>"};
        Document d;
        d.newProject();
        d.addShape("ellipse");
        const auto before = d.projectBytes();
        const auto revision = d.historyRevision();
        QSignalSpy reset(&d, &QAbstractItemModel::modelReset);
        for (const auto &bad : badShapes) {
            const auto bytes = project(rectangle() + bad);
            const auto parsed = NativeDesign::read(bytes, true);
            QVERIFY2(!parsed.error.isEmpty(), bytes.constData());
            QVERIFY(parsed.elements.isEmpty());
            QVERIFY(!d.importFile(write("invalid.mage", bytes)));
            QCOMPARE(d.projectBytes(), before);
            QCOMPARE(d.historyRevision(), revision);
            QCOMPARE(reset.count(), 0);
        }
        auto mirrored = project(rectangle());
        mirrored.replace("MirrorY=\"False\"", "MirrorY=\"True\"");
        QVERIFY(!NativeDesign::read(mirrored, true).error.isEmpty());
        QVERIFY(!NativeDesign::read(project(rectangle()), false).error.isEmpty());
        auto newer = project(rectangle());
        newer.replace("FormatVersion=\"1\"", "FormatVersion=\"2\"");
        QVERIFY(!NativeDesign::read(newer, true).error.isEmpty());
        QVERIFY(!NativeDesign::read(
                     "<!DOCTYPE MillMageProject [<!ENTITY x 'X'>]>" + project(rectangle()), true)
                     .error.isEmpty());
    }
    void limitsAndCombinedPreflightNeverResetLiveDocument() {
        QVERIFY(!NativeDesign::read(QByteArray(16 * 1024 * 1024 + 1, ' '), true).error.isEmpty());
        QByteArray nested;
        for (int i = 0; i < 40; ++i)
            nested += "<Shape Type=\"Group\"><Children>";
        nested += rectangle();
        for (int i = 0; i < 40; ++i)
            nested += "</Children></Shape>";
        QVERIFY(NativeDesign::read(project(nested), true).error.contains("tief"));
        const auto tooMany = project(rectangle().repeated(2001));
        QVERIFY(!NativeDesign::read(tooMany, true).error.isEmpty());
        const auto vertices =
            project("<Shape Type=\"Path\"><VertList>" + QByteArray("V0 0").repeated(100001) +
                    "</VertList><PrimList>L0 1</PrimList></Shape>");
        QVERIFY(NativeDesign::read(vertices, true).error.contains("Pfadpunkte"));
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto before = d.projectBytes();
        const auto revision = d.historyRevision();
        QSignalSpy reset(&d, &QAbstractItemModel::modelReset);
        QVERIFY(!d.importFile(write("capacity.mage", project(rectangle().repeated(2000)))));
        QCOMPARE(d.projectBytes(), before);
        QCOMPARE(d.historyRevision(), revision);
        QCOMPARE(reset.count(), 0);
        d.setLayerSetting(0, "mode", "Schnitt");
        const auto cutOnly = d.projectBytes();
        QVERIFY(!d.importFile(write("no-engrave.mage", project(bitmap()))));
        QCOMPARE(d.projectBytes(), cutOnly);
        QCOMPARE(reset.count(), 0);
    }
};
QTEST_MAIN(NativeImportTest)
#include "nativeimport_test.moc"
