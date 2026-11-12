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
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(1)), QPointF(8, 6));
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(2)), QPointF(15, 8));
        QCOMPARE(QPointF(curve.elements[0].path.elementAt(3)), QPointF(15, 11));
        const auto disabled =
            NativeDesign::read(project("<Shape Type=\"Path\"><VertList>V10 5c1x1V20 "
                                       "9c0x1</VertList><PrimList>B0 1</PrimList></Shape>"),
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
            "<Shape Type=\"Path\"><VertList>V0 0c1x2V1 1</VertList><PrimList>B0 1</PrimList></Shape>",
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
