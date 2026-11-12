#include "document.h"
#include "imageprocessing.h"
#include "plotimport.h"
#include "recovery.h"
#include "registration.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <numbers>
class ExpansionTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QUrl write(QString name, QByteArray data) {
        auto path = temp.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
            return {};
        return QUrl::fromLocalFile(path);
    }
    QUrl bitmap() {
        QImage image(20, 10, QImage::Format_ARGB32);
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 20; ++x)
                image.setPixelColor(x, y, QColor(x * 255 / 19, x * 255 / 19, x * 255 / 19));
        auto path = temp.filePath("image.png");
        image.save(path);
        return QUrl::fromLocalFile(path);
    }
    static QByteArray rdNumber(quint64 value, int count = 5) {
        QByteArray bytes(count, '\0');
        for (int i = count - 1; i >= 0; --i) { bytes[i] = char(value % 128); value /= 128; }
        return bytes;
    }
    static QByteArray rdRelative(int value) { return rdNumber(value & 16383, 2); }
    static QByteArray rdCommand(int code, const QByteArray &args = {}) {
        return QByteArray(1, char(code)) + args;
    }
    static QByteArray rdEncode(const QByteArray &decoded, int magic = 0x88) {
        QByteArray result;
        for (quint8 byte : decoded) {
            const int rotated = (byte & 126) + (byte % 2) * 128 + byte / 128;
            result.append(char(((rotated ^ magic) + 1) & 255));
        }
        return result;
    }
    static QByteArray rdJob(const QByteArray &body, int magic = 0x88) {
        return rdEncode(QByteArray::fromHex("d812f0") + body + QByteArray::fromHex("ebe700d7"), magic);
    }
    static QByteArray rdRectangle() {
        return rdCommand(0x88, rdNumber(10000) + rdNumber(20000)) +
               rdCommand(0xa8, rdNumber(30000) + rdNumber(20000)) +
               rdCommand(0xa8, rdNumber(30000) + rdNumber(30000)) +
               rdCommand(0xa8, rdNumber(10000) + rdNumber(30000)) +
               rdCommand(0xa8, rdNumber(10000) + rdNumber(20000));
    }
  private slots:
    void ruidaAbsoluteRelativeAndYOrientation() {
        QCOMPARE(rdEncode(QByteArray::fromHex("d812f0")),QByteArray::fromHex("d29bfa"));
        QFile fixture(QFINDTESTDATA("fixtures/ruida/rectangle.rd"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto fixed=PlotImport::ruida(fixture.readAll());
        QVERIFY2(fixed.error.isEmpty(),qPrintable(fixed.error));
        QCOMPARE(fixed.paths[0].boundingRect(),QRectF(0,0,20,10));
        QCOMPARE(fixed.paths[0].length(),60.);
        QCOMPARE(QPointF(fixed.paths[0].elementAt(0)),QPointF(0,10));
        const auto body=rdCommand(0x88,rdNumber(10000)+rdNumber(20000))+
            rdCommand(0xa8,rdNumber(30000)+rdNumber(20000))+
            rdCommand(0xa9,rdRelative(0)+rdRelative(5000))+
            rdCommand(0xaa,rdRelative(-6000))+rdCommand(0xab,rdRelative(-2000))+
            rdCommand(0x89,rdRelative(1000)+rdRelative(-1000))+
            rdCommand(0xa9,rdRelative(2000)+rdRelative(3000));
        const QList<QPointF> expected{{0,5},{20,5},{20,0},{14,0},{14,2},{15,3},{17,0}};
        for(int magic:{0x88,0x11}) {
            const auto result=PlotImport::ruida(rdJob(body,magic));
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
            QCOMPARE(result.paths.size(),1);
            const auto &path=result.paths[0];
            QCOMPARE(path.elementCount(),expected.size());
            for(int i=0;i<expected.size();++i)QCOMPARE(QPointF(path.elementAt(i)),expected[i]);
            QVERIFY(path.elementAt(5).isMoveTo());
            QCOMPARE(result.interpretation,QString("2 getrennte Schnittwege"));
        }
        const auto extremes=PlotImport::ruida(rdJob(rdCommand(0x88,rdNumber(100000)+rdNumber(100000))+
            rdCommand(0x8a,rdRelative(1000))+rdCommand(0x8b,rdRelative(2000))+
            rdCommand(0xa9,rdRelative(8191)+rdRelative(-8192))));
        QVERIFY2(extremes.error.isEmpty(),qPrintable(extremes.error));
        QCOMPARE(extremes.paths[0].boundingRect(),QRectF(0,0,8.191,8.192));
        QCOMPARE(QPointF(extremes.paths[0].elementAt(0)),QPointF(0,0));
    }
    void ruidaMetadataNeverBecomesMotionOrParameters() {
        const QByteArray zero(5,'\0');
        const auto metadata=rdCommand(0xf1,QByteArray::fromHex("0200"))+
            rdCommand(0xe7,QByteArray(1,char(3))+rdNumber(1000000)+rdNumber(1000000))+
            rdCommand(0xe7,QByteArray(1,char(4))+rdNumber(1,2)+rdNumber(1,2)+zero+zero)+
            rdCommand(0xc9,QByteArray(1,char(2))+rdNumber(33333000))+
            rdCommand(0xc6,QByteArray::fromHex("027f7f"));
        const auto plain=PlotImport::ruida(rdJob(rdRectangle()));
        const auto result=PlotImport::ruida(rdJob(metadata+rdRectangle()));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
        QCOMPARE(result.paths,plain.paths);
        QCOMPARE(result.paths[0].boundingRect(),QRectF(0,0,20,10));
        QCOMPARE(result.paths[0].length(),60.);
    }
    void ruidaMalformedAndUnsupportedAreAtomic() {
        const QList<QByteArray> bad{
            {},QByteArray(".RDFILEVER8.0.01"),rdEncode(QByteArray::fromHex("d812f0")),
            rdJob(rdCommand(0xa8,rdNumber(1)+rdNumber(1))),
            rdJob(rdCommand(0x89,rdRelative(1)+rdRelative(1))),
            rdJob(rdRectangle()+rdCommand(0xa9,QByteArray(3,'\0'))),
            rdJob(rdRectangle()+rdCommand(0x90,QByteArray(4,'\0'))),
            rdJob(rdRectangle()+rdCommand(0xc9,QByteArray::fromHex("02"))),
            rdJob(rdCommand(0xe7,QByteArray(1,char(6))+rdNumber(1)+rdNumber(0))+rdRectangle()),
            rdJob(rdCommand(0xf1,QByteArray(1,char(3))+rdNumber(0)+rdNumber(1))+rdRectangle()),
            rdJob(rdCommand(0xe7,QByteArray(1,char(0x55))+QByteArray(1,'\0')+rdNumber(1))+rdRectangle()),
            rdJob(rdCommand(0xe7,QByteArray(1,char(4))+rdNumber(2,2)+rdNumber(1,2)+QByteArray(10,'\0'))+rdRectangle()),
            rdJob(rdRectangle()+rdCommand(0x88,rdNumber(2000001)+rdNumber(0))),
            rdJob(rdRectangle()+rdCommand(0xe7,QByteArray(1,'\0'))+rdCommand(0xaa,rdRelative(1))),
            rdJob(rdRectangle()+rdCommand(0xf0)),
            rdEncode(QByteArray::fromHex("d812f0")+rdRectangle()+QByteArray::fromHex("eb")+rdCommand(0xaa,rdRelative(1))+QByteArray::fromHex("d7")),
            rdJob(rdRectangle())+rdEncode(rdCommand(0xf0)),
            rdEncode(QByteArray::fromHex("d812f0")+rdRectangle()+QByteArray::fromHex("ebe700d700"))};
        Document d;d.newProject();d.addShape("ellipse");const auto before=d.projectBytes();
        const auto revision=d.historyRevision();QSignalSpy resets(&d,&QAbstractItemModel::modelReset);
        for(const auto &bytes:bad){
            const auto result=PlotImport::ruida(bytes);
            QVERIFY2(!result.error.isEmpty(),bytes.toHex().constData());QVERIFY(result.paths.isEmpty());
            QVERIFY(!d.importFile(write("bad.rd",bytes)));
            QCOMPARE(d.projectBytes(),before);QCOMPARE(d.historyRevision(),revision);QCOMPARE(resets.count(),0);
        }
    }
    void ruidaProjectRoundtripPreservesOwnSettings() {
        Document d;d.newProject();QVERIFY(d.addPage("RD-Ziel"));
        QVERIFY(d.setMetadata("Mein Projekt","Meine Notizen"));
        const auto own=QUrl::fromLocalFile(temp.filePath("own-rd.blazer"));QVERIFY(d.save(own));
        const auto before=d.projectBytes();const auto layers=d.layers();const auto material=d.material();
        QVERIFY(d.importFile(write("rectangle.rd",rdJob(rdRectangle()))));
        QCOMPARE(d.count(),1);QCOMPARE(d.items()[0].page,1);QCOMPARE(d.items()[0].bounds,QRectF(0,0,20,10));
        QCOMPARE(d.layers(),layers);QCOMPARE(d.material(),material);QCOMPARE(d.fileUrl(),own);
        QCOMPARE(d.title(),QString("Mein Projekt"));QCOMPARE(d.notes(),QString("Meine Notizen"));
        QVERIFY(d.items()[0].settings.isEmpty());QVERIFY(d.job().valid());
        const auto saved=d.projectBytes();const auto code=d.job().gcode();Document restored;
        QVERIFY(restored.restoreBytes(saved));QCOMPARE(restored.job().gcode(),code);
        d.undo();QCOMPARE(d.projectBytes(),before);d.redo();QCOMPARE(d.projectBytes(),saved);
    }
    void ruidaWorkLimitsAndDisconnectedRaster() {
        QVERIFY(!PlotImport::ruida(QByteArray(16*1024*1024+1,' ')).error.isEmpty());
        const auto tooMany=rdEncode(QByteArray::fromHex("d812f0")+rdCommand(0xc6,QByteArray::fromHex("010000")).repeated(100001)+QByteArray::fromHex("ebd7"));
        QVERIFY(PlotImport::ruida(tooMany).error.contains("Befehlsgrenze"));
        const auto segment=rdCommand(0x88,rdNumber(0)+rdNumber(0))+rdCommand(0xa8,rdNumber(10000)+rdNumber(5000));
        const auto raster=PlotImport::ruida(rdJob(segment.repeated(3000)));
        QVERIFY2(raster.error.isEmpty(),qPrintable(raster.error));QCOMPARE(raster.paths.size(),1);
        QCOMPARE(raster.paths[0].elementCount(),6000);
        QVERIFY(PlotImport::ruida(rdJob(segment.repeated(50001))).error.contains("Befehlsgrenze"));
    }

    void hpglScaledProjectRoundtripAndJob() {
        Document d;
        d.newProject();
        d.addShape("rect");
        QVERIFY(d.addPage("Skaliert"));
        const auto before = d.projectBytes();
        const auto file = write("scaled-closed.plt", "IN;SP1;IP400,800,1200,1200;SC0,10,0,10;"
                                                     "PU0,0;PD10,0,10,10,0,10,0,0;SP0;");
        QVERIFY2(d.importFile(file), qPrintable(d.message()));
        QCOMPARE(d.count(), 2);
        QCOMPARE(d.items().last().page, 1);
        QCOMPARE(d.items().last().bounds, QRectF(0, 0, 20, 10));
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QCOMPARE(d.job().workLength, 60.);
        const auto code = d.job().gcode();
        const auto imported = d.projectBytes();
        const auto own = QUrl::fromLocalFile(temp.filePath("scaled.blazer"));
        QVERIFY(d.save(own));
        Document restored;
        QVERIFY(restored.open(own));
        QCOMPARE(restored.job().gcode(), code);
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        QCOMPARE(d.fileUrl(), own);
        d.redo();
        QCOMPARE(d.projectBytes(), imported);
        QCOMPARE(d.job().gcode(), code);
    }
    void hpglIndependentScaledReferenceCorpus() {
        const auto file = QFINDTESTDATA("fixtures/hpgl/scaling-expectations.json");
        QVERIFY(!file.isEmpty());
        QFile reference(file);
        QVERIFY(reference.open(QIODevice::ReadOnly));
        const auto cases = QJsonDocument::fromJson(reference.readAll()).object()["cases"].toArray();
        QCOMPARE(cases.size(), 3);
        for (const auto &entry : cases) {
            const auto object = entry.toObject();
            QFile input(QFileInfo(file).dir().filePath(object["file"].toString()));
            QVERIFY(input.open(QIODevice::ReadOnly));
            const auto result = PlotImport::hpgl(input.readAll());
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            const auto expectedPaths = object["paths"].toArray();
            QCOMPARE(result.paths.size(), expectedPaths.size());
            for (int p = 0; p < expectedPaths.size(); ++p) {
                const auto points = expectedPaths[p].toArray();
                QCOMPARE(result.paths[p].elementCount(), points.size());
                for (int i = 0; i < points.size(); ++i) {
                    const auto point = points[i].toArray();
                    const auto actual = QPointF(result.paths[p].elementAt(i));
                    QVERIFY(
                        QLineF(actual, QPointF(point[0].toDouble(), point[1].toDouble())).length() <
                        1e-9);
                }
            }
        }
    }
    void hpglExplicitScalesAndReflections() {
        const auto anisotropic =
            PlotImport::hpgl("IN;SP1;IP400.4,800.4,1199.6,1199.6;SC0,10,0,10;PU0,0;PD10,0,10,10;");
        QVERIFY2(anisotropic.error.isEmpty(), qPrintable(anisotropic.error));
        QCOMPARE(anisotropic.paths.size(), 1);
        const auto &p = anisotropic.paths[0];
        QCOMPARE(p.boundingRect(), QRectF(0, 0, 20, 10));
        QCOMPARE(QPointF(p.elementAt(0)), QPointF(0, 0));
        QCOMPARE(QPointF(p.elementAt(1)), QPointF(20, 0));
        QCOMPARE(QPointF(p.elementAt(2)), QPointF(20, 10));
        const auto mm = PlotImport::hpgl("IN;SP1;SC0,40,0,40,2;PU0,0;PD10,0,10,5;");
        QVERIFY2(mm.error.isEmpty(), qPrintable(mm.error));
        QCOMPARE(mm.paths[0].boundingRect(), QRectF(0, 0, 10, 5));
        const auto mirror = PlotImport::hpgl("IN;SP1;SC0,40,0,-40,2;PU0,0;PD10,0,10,5;");
        QVERIFY2(mirror.error.isEmpty(), qPrintable(mirror.error));
        QCOMPARE(QPointF(mirror.paths[0].elementAt(0)), QPointF(0, 5));
        QCOMPARE(QPointF(mirror.paths[0].elementAt(2)), QPointF(10, 0));
        const auto corrected = PlotImport::hpgl("IN;SP1;IP0,0,0,400;SC0,1,0,1;PU0,0;PD1,0,1,1;");
        QVERIFY2(corrected.error.isEmpty(), qPrintable(corrected.error));
        QCOMPARE(corrected.paths[0].boundingRect(), QRectF(0, 0, .025, 10));
    }
    void hpglIsotropicPlacementAndReversedReferences() {
        const QByteArray frame = "IN;SP1;PU0,0;PD800,0,800,400,0,400,0,0;PU;";
        for (const auto &row : QList<QPair<QByteArray, QRectF>>{
                 {"IP0,0,800,400;SC0,10,0,10,1;", {5, 0, 10, 10}},
                 {"IP0,0,800,400;SC0,10,0,10,1,0,100;", {0, 0, 10, 10}},
                 {"IP0,0,800,400;SC0,10,0,10,1,100,0;", {10, 0, 10, 10}},
                 {"IP800,400,0,0;SC0,10,0,10,1,0,100;", {10, 0, 10, 10}},
                 {"IP800,400,0,0;SC0,10,0,10,1,100,0;", {0, 0, 10, 10}}}) {
            const auto result = PlotImport::hpgl(frame + row.first + "PU0,0;PD10,0,10,10;");
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.paths.size(), 2);
            QCOMPARE(result.paths[1].boundingRect(), row.second);
        }
        const auto tall = PlotImport::hpgl("IN;SP1;PU0,0;PD400,0,400,800,0,800,0,0;PU;"
                                           "IP0,0,400,800;SC0,10,0,10,1,50,25;PU0,0;PD10,0,10,10;");
        QVERIFY2(tall.error.isEmpty(), qPrintable(tall.error));
        QCOMPARE(tall.paths[1].boundingRect(), QRectF(0, 2.5, 10, 10));
        const auto flipped =
            PlotImport::hpgl(frame + "IP0,0,800,400;SC10,0,0,10,1;PU0,0;PD10,0,10,10;");
        QVERIFY2(flipped.error.isEmpty(), qPrintable(flipped.error));
        QCOMPARE(QPointF(flipped.paths[1].elementAt(0)), QPointF(15, 0));
        QCOMPARE(QPointF(flipped.paths[1].elementAt(1)), QPointF(5, 0));
    }
    void hpglScaleChangesPreservePhysicalPenAndModes() {
        const auto result =
            PlotImport::hpgl("IN;SP1;PU400,400;PD800,400;"
                             "IP0,0,800,400;SC0,20,0,10;PR5,0;IP0,0,1600,400;PR5,0;SC;PR200,0;");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.paths.size(), 1);
        const auto &p = result.paths[0];
        QCOMPARE(p.elementCount(), 5);
        const QList<double> expected{0, 10, 15, 25, 30};
        for (int i = 0; i < p.elementCount(); ++i) {
            QCOMPARE(p.elementAt(i).x, expected[i]);
            QCOMPARE(p.elementAt(i).y, 0.);
        }
        QByteArray repeated = "INSP1PU400,400PD800,400;";
        for (int i = 0; i < 2000; ++i)
            repeated += "IP7,11,997,683;SC0,17,0,23;IP-13,19,1451,937;SC-5,40,9,31;SC;";
        repeated += "PR200,0;";
        const auto stable = PlotImport::hpgl(repeated);
        QVERIFY2(stable.error.isEmpty(), qPrintable(stable.error));
        QCOMPARE(stable.paths.size(), 1);
        QCOMPARE(stable.paths[0].elementCount(), 3);
        QCOMPARE(stable.paths[0].elementAt(2).x, 15.);
        const auto tracking = PlotImport::hpgl("IN;SP1;IP0,0,800,400;SC0,20,0,10;PU0,0;PD10,0;"
                                               "IP400,400;PR5,0;PA0,0;");
        QVERIFY2(tracking.error.isEmpty(), qPrintable(tracking.error));
        QCOMPARE(tracking.paths.size(), 1);
        QCOMPARE(tracking.paths[0].elementAt(2).x, 15.);
        QCOMPARE(QPointF(tracking.paths[0].elementAt(3)), QPointF(10, 10));
        const auto reset = PlotImport::hpgl("IN;SP1;SC0,40,0,40,2;PU0,0;PD10,0;"
                                            "IN;SP1;PU0,400;PD400,400;");
        QVERIFY2(reset.error.isEmpty(), qPrintable(reset.error));
        QCOMPARE(reset.paths.size(), 2);
        QCOMPARE(reset.paths[1].boundingRect(), QRectF(0, 10, 10, 0));
        const auto signs = PlotImport::hpgl("INSP1PU-400-800PD+400-800+400-400;");
        QVERIFY2(signs.error.isEmpty(), qPrintable(signs.error));
        QCOMPARE(signs.paths[0].boundingRect(), QRectF(0, 0, 20, 10));
    }
    void hpglScaledCurvesAndStatefulErrors() {
        const auto ellipse = PlotImport::hpgl("IN;SP1;IP0,0,800,400;SC0,10,0,10;PU5,5;CI5,90;"
                                              "PR;PD5,0;");
        QVERIFY2(ellipse.error.isEmpty(), qPrintable(ellipse.error));
        QCOMPARE(ellipse.paths.size(), 2);
        QCOMPARE(ellipse.paths[0].boundingRect(), QRectF(0, 0, 20, 10));
        QCOMPARE(QPointF(ellipse.paths[1].elementAt(0)), QPointF(10, 5));
        QCOMPARE(QPointF(ellipse.paths[1].elementAt(1)), QPointF(20, 5));
        const auto curve = PlotImport::hpgl("IN;SP1;IP0,0,800,400;SC0,10,0,10;PU0,0;PD;"
                                            "BZ0,10,10,10,10,0;");
        QVERIFY2(curve.error.isEmpty(), qPrintable(curve.error));
        QCOMPARE(curve.paths[0].elementAt(1).type, QPainterPath::CurveToElement);
        QCOMPARE(QPointF(curve.paths[0].elementAt(1)), QPointF(0, 10));
        QCOMPARE(QPointF(curve.paths[0].elementAt(2)), QPointF(20, 10));
        QCOMPARE(QPointF(curve.paths[0].elementAt(3)), QPointF(20, 0));
        const QList<QByteArray> invalid = {"IP0,0,100;",
                                           "IP80001,0,90000,100;",
                                           "SC0,10,0,10;",
                                           "SC0,0,0,40,2;",
                                           "SC0,40,0,40,2,50,50;",
                                           "SC0,10,0,10,3;",
                                           "IP0,0,800,400;SC0,0,0,10;",
                                           "IP0,0,800,400;SC0,10,0,10,1,50;",
                                           "IP0,0,800,400;SC0,10,0,10,1,101,50;",
                                           "SC0,400000,0,40,2;PA1,0;",
                                           "IP0,0,800,400;SC0,20,0,10;IP;",
                                           "SC1000000000,1000000000,0,40,2;"};
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto before = d.projectBytes();
        const auto revision = d.historyRevision();
        for (const auto &bad : invalid) {
            const auto input = "IN;SP1;PU400,400;PD800,400;" + bad;
            const auto parsed = PlotImport::hpgl(input);
            QVERIFY2(!parsed.error.isEmpty(), input.constData());
            QVERIFY(parsed.paths.isEmpty());
            QVERIFY2(!d.importFile(write("scale-invalid.plt", input)), input.constData());
            QCOMPARE(d.projectBytes(), before);
            QCOMPARE(d.historyRevision(), revision);
        }
    }
    void gcodeLaserFilteringAndBlockOrder() {
        const auto result =
            PlotImport::gcode("G21 G90 G17 G54 G94\nG0 X10 Y20\nM4 S0\nG1 X11 Y20 F1000\n"
                              "G1 X21 S100\nG1 Y30 S500\nG1 X11 S0\nG1 Y40 S100\n"
                              "G0 X50 Y60\nM5\nG1 X60 S100\nM3\nG1 X70\nM2\nM999 G28\n");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.paths.size(), 3);
        QCOMPARE(result.interpretation, QString("Laser-/Spindelpfade"));
        const auto &p = result.paths[0];
        QCOMPARE(p.elementCount(), 3);
        QCOMPARE(QPointF(p.elementAt(0)), QPointF(0, 0));
        QCOMPARE(QPointF(p.elementAt(1)), QPointF(10, 0));
        QCOMPARE(QPointF(p.elementAt(2)), QPointF(10, 10));
        QCOMPARE(result.paths[1].boundingRect(), QRectF(0, 10, 0, 10));
        QCOMPARE(result.paths[2].boundingRect(), QRectF(49, 40, 10, 0));
        const auto a = PlotImport::gcode("G0X0Y0\nG1X10S100M3\n");
        const auto b = PlotImport::gcode("G0X0Y0\nM3S100G1X10\n");
        QVERIFY(a.error.isEmpty());
        QVERIFY(b.error.isEmpty());
        QCOMPARE(a.paths, b.paths);
        // Source power determines geometry visibility, never a copied operation.
        QVERIFY(!PlotImport::gcode("G0X0Y0\nM3S0\nG1X10\n").error.isEmpty());
        QVERIFY(!PlotImport::gcode("G0X0Y0\nS100\nG1X10\n").error.isEmpty());
    }
    void gcodeUnitsModesCommentsAndProgramEnd() {
        const auto result =
            PlotImport::gcode("%\r\nN1 G20 G90 G0 X+1. 0 Y2 (M3 S100 ignored)\r\n"
                              "N2 G91 G1 X.5 Y.25\r\nN3 G21 G90 X50.8 Y63.5;M5 ignored\r\n"
                              "G4 P.2\r\nM0\r\nM30\r\nG9999 [ignored tail]\r\n%\r\n");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.interpretation.contains("ohne M/S"));
        QCOMPARE(result.paths.size(), 1);
        const auto &p = result.paths[0];
        QCOMPARE(p.elementCount(), 3);
        QVERIFY(QLineF(QPointF(p.elementAt(0)), QPointF(0, 0)).length() < 1e-8);
        QVERIFY(QLineF(QPointF(p.elementAt(1)), QPointF(12.7, 6.35)).length() < 1e-8);
        QVERIFY(QLineF(QPointF(p.elementAt(2)), QPointF(25.4, 12.7)).length() < 1e-8);
        const auto separate = PlotImport::gcode("G0X10\nG0Y20\nG1X30\nG80\nG1Y30\n");
        QVERIFY2(separate.error.isEmpty(), qPrintable(separate.error));
        QCOMPARE(separate.paths[0].boundingRect(), QRectF(0, 0, 20, 10));
        QVERIFY(PlotImport::gcode("%\nG0X0Y0\nG1X10\n").error.contains("Prozent"));
        // No machine location is invented for a first drawing move.
        QVERIFY(!PlotImport::gcode("G1X10Y20\n").error.isEmpty());
        const auto relativeOnly =
            PlotImport::gcode("G91\nT0\nM4\nM8\nG0X0Y0\nG1X5S1000F3000\nM9\nM5\nG0X-5Y0\nM2\n");
        QVERIFY2(relativeOnly.error.isEmpty(), qPrintable(relativeOnly.error));
        QCOMPARE(relativeOnly.paths[0].boundingRect(), QRectF(0, 0, 5, 0));
        QVERIFY(!PlotImport::gcode("G91G1X5\nG90G0X10Y10\n").error.isEmpty());
        const auto anchor = PlotImport::gcode("G91G0X5\nG90G0X10Y20\nG1X20\n");
        QVERIFY2(anchor.error.isEmpty(), qPrintable(anchor.error));
        QCOMPARE(anchor.paths[0].boundingRect(), QRectF(0, 0, 10, 0));
        QVERIFY(!PlotImport::gcode("G0X10\nG91G1Y20\n").error.isEmpty());
        QVERIFY(PlotImport::gcode(QByteArray("\xEF\xBB\xBF") + "G0X0Y0\nG1X10\n").error.isEmpty());
        const auto ended = PlotImport::gcode("G0X0Y0\nG1X10\nM2\nM3S100\n");
        QVERIFY(ended.error.isEmpty());
        QVERIFY(ended.interpretation.contains("ohne M/S"));
    }
    void gcodeArcCentresRadiusDirectionAndTurns() {
        const auto relative = PlotImport::gcode("G0X10Y0\nG3X0Y10I-10J0F100\n");
        const auto absolute = PlotImport::gcode("G0X10Y0\nG90.1G3X0Y10I0J0\n");
        const auto radius = PlotImport::gcode("G0X10Y0\nG3X0Y10R10\n");
        QVERIFY2(relative.error.isEmpty(), qPrintable(relative.error));
        QVERIFY2(absolute.error.isEmpty(), qPrintable(absolute.error));
        QVERIFY2(radius.error.isEmpty(), qPrintable(radius.error));
        QCOMPARE(relative.paths, absolute.paths);
        const auto &p = relative.paths[0];
        QCOMPARE(p.boundingRect(), QRectF(0, 0, 10, 10));
        QVERIFY(p.elementAt(1).x < 10 && p.elementAt(1).y > 0);
        QCOMPARE(QPointF(p.elementAt(p.elementCount() - 1)), QPointF(0, 10));
        const auto longArc = PlotImport::gcode("G0X10Y0\nG3X0Y10R-10\n");
        QVERIFY2(longArc.error.isEmpty(), qPrintable(longArc.error));
        QVERIFY(longArc.paths[0].boundingRect().width() > 19.98);
        QVERIFY(longArc.paths[0].boundingRect().height() > 19.98);
        QVERIFY(longArc.paths[0].elementAt(1).x > longArc.paths[0].elementAt(0).x);
        auto signedArea = [](const QPainterPath &path) {
            double area = 0;
            for (int i = 1; i < path.elementCount(); ++i) {
                const auto a = path.elementAt(i - 1), b = path.elementAt(i);
                area += (a.x * b.y - b.x * a.y) / 2;
            }
            return area;
        };
        const auto cw = PlotImport::gcode("G0X10Y0\nG2I-10J0\n");
        const auto ccw = PlotImport::gcode("G0X10Y0\nG3I-10J0P2\n");
        QVERIFY2(cw.error.isEmpty(), qPrintable(cw.error));
        QVERIFY2(ccw.error.isEmpty(), qPrintable(ccw.error));
        QVERIFY(std::abs(signedArea(cw.paths[0]) + 100 * std::numbers::pi) < 1.);
        QVERIFY(std::abs(signedArea(ccw.paths[0]) - 200 * std::numbers::pi) < 2.);
        QCOMPARE(QPointF(cw.paths[0].elementAt(0)),
                 QPointF(cw.paths[0].elementAt(cw.paths[0].elementCount() - 1)));
        const QList<QByteArray> invalid = {"G2X0Y10R1",        "G2X10Y0R10",       "G3I0J0",
                                           "G90.1G3X0Y10I0",   "G3X0Y10I-10J0R10", "G3I-10J0P1.5",
                                           "G3X0Y10.006I-10J0"};
        for (const auto &bad : invalid)
            QVERIFY2(!PlotImport::gcode("G0X10Y0\n" + bad + "\n").error.isEmpty(), bad.constData());
    }
    void gcodeActualBlazerOutputAndAtomicDocumentImport() {
        Document source;
        source.newProject();
        source.addShape("rect");
        source.setProperty("x", 20.);
        source.setProperty("y", 30.);
        source.setProperty("width", 10.);
        source.setProperty("height", 5.);
        const auto generated = source.job().gcode().join('\n').toUtf8();
        Document d;
        d.newProject();
        QVERIFY(d.addPage("GCode"));
        const auto empty = d.projectBytes();
        QVERIFY2(d.importFile(write("blazer.NC", generated)), qPrintable(d.message()));
        QCOMPARE(d.count(), 1);
        QCOMPARE(d.items()[0].page, 1);
        QCOMPARE(d.items()[0].bounds, QRectF(0, 0, 10, 5));
        QCOMPARE(d.job().workLength, source.job().workLength);
        const auto bytes = d.projectBytes();
        const auto code = d.job().gcode();
        Document restored;
        QVERIFY(restored.restoreBytes(bytes));
        QCOMPARE(restored.job().gcode(), code);
        d.undo();
        QCOMPARE(d.projectBytes(), empty);
        d.redo();
        QCOMPARE(d.projectBytes(), bytes);
        const QList<QByteArray> invalid = {"G28",
                                           "G53G0X0Y0",
                                           "G55",
                                           "G92X0",
                                           "G10L20P1X0",
                                           "G18",
                                           "G41D1",
                                           "G43H1",
                                           "G81X1Y1",
                                           "G1Z0",
                                           "M6T1",
                                           "M98P1",
                                           "G1X#1",
                                           "G1X[1+2]",
                                           "G1X1e2",
                                           "G1X10X20",
                                           "G0G1X10",
                                           "M3M5",
                                           "G20G21",
                                           "F0",
                                           "S-1",
                                           "N1.5G1X10",
                                           "G1X2001",
                                           "G4P1X10",
                                           "G1X10*42",
                                           "/G1X10",
                                           "G1X10(unclosed",
                                           "G1X10(nested(comment))",
                                           "T1.5",
                                           "T256"};
        for (const auto &bad : invalid) {
            const auto revision = d.historyRevision();
            const auto parsed = PlotImport::gcode("G0X0Y0\nG1X10\n" + bad + "\n");
            QVERIFY2(!parsed.error.isEmpty(), bad.constData());
            QVERIFY(parsed.paths.isEmpty());
            QVERIFY(!d.importFile(write("bad.gc", "G0X0Y0\nG1X10\n" + bad + "\n")));
            QCOMPARE(d.projectBytes(), bytes);
            QCOMPARE(d.historyRevision(), revision);
        }
        JobPlan raster;
        raster.moves = {{{0, 0}, {10, 20}, 1000, 0, 0, true},
                        {{10, 20}, {20, 20}, 1000, 20, 0, false},
                        {{20, 20}, {30, 20}, 1000, 0, 0, false},
                        {{30, 20}, {40, 20}, 1000, 50, 0, false}};
        const auto inlinePower = PlotImport::gcode(raster.gcode().join('\n').toUtf8());
        QVERIFY2(inlinePower.error.isEmpty(), qPrintable(inlinePower.error));
        QCOMPARE(inlinePower.paths.size(), 2);
        QCOMPARE(inlinePower.paths[0].boundingRect(), QRectF(0, 0, 10, 0));
        QCOMPARE(inlinePower.paths[1].boundingRect(), QRectF(20, 0, 10, 0));
    }
    void gcodeResourceBudgets() {
        QVERIFY(!PlotImport::gcode(QByteArray(16 * 1024 * 1024 + 1, ' ')).error.isEmpty());
        QVERIFY(!PlotImport::gcode(QByteArray(4097, ' ')).error.isEmpty());
        QVERIFY(!PlotImport::gcode(QByteArray(100001, '\n')).error.isEmpty());
        const auto hidden = PlotImport::gcode("G0X1000Y0\nM4S0\nG3I-1000J0P1000\n");
        QVERIFY2(hidden.error.contains("Unterteilung"), qPrintable(hidden.error));
        QByteArray many = "G0X0Y0\n";
        for (int i = 0; i < 2001; ++i)
            many += "G0X0Y0\nG1X10\n";
        const auto parsed = PlotImport::gcode(many);
        QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
        QCOMPARE(parsed.paths.size(), 2001);
        Document d;
        d.newProject();
        QVERIFY(d.importFile(write("many.gcode", many)));
        QCOMPARE(d.count(), 1);
        d.setProperty("y", 1.);
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QCOMPARE(d.job().workLength, 20010.);
        QCOMPARE(d.items()[0].path.elementCount(), 4002);
        const auto words = PlotImport::gcode(QByteArray("G0G21G90G17G54S0\n").repeated(83334));
        QVERIFY2(words.error.contains("Wortanzahl"), qPrintable(words.error));
        many = "G0X0Y0\n";
        for (int i = 0; i < 99998; ++i)
            many += i % 2 ? "G1X0\n" : "G1X10\n";
        many += "G3I-1J0\n";
        const auto elements = PlotImport::gcode(many);
        QVERIFY2(elements.error.contains("Zu viele Pfadelemente"), qPrintable(elements.error));
    }
    void hpglMillimetresOrientationAndModalCoordinates() {
        const auto result = PlotImport::hpgl("IN;SP1;PU400,800;PD1200,800,1200,1200;"
                                             "PU;PR-800,0;PD800,0;SP0;PD800,800;");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.paths.size(), 2);
        // An asymmetric L is not mirrored: HPGL and editor world both use +Y up.
        const auto &l = result.paths[0];
        QCOMPARE(l.elementCount(), 3);
        QCOMPARE(QPointF(l.elementAt(0)), QPointF(0, 0));
        QCOMPARE(QPointF(l.elementAt(1)), QPointF(20, 0));
        QCOMPARE(QPointF(l.elementAt(2)), QPointF(20, 10));
        QCOMPARE(result.paths[1].boundingRect(), QRectF(0, 10, 20, 0));
        // Separate zero-height strokes must both contribute to the common bounds.
        const auto lines = PlotImport::hpgl("in sp1 pu400,400 pd800,400 pu400,800 pd800,800");
        QVERIFY2(lines.error.isEmpty(), qPrintable(lines.error));
        QCOMPARE(lines.paths.size(), 2);
        QCOMPARE(QPointF(lines.paths[0].elementAt(0)), QPointF(0, 0));
        QCOMPARE(QPointF(lines.paths[1].elementAt(0)), QPointF(0, 10));
    }
    void hpglCirclesArcsAndBezierState() {
        const auto circle = PlotImport::hpgl("IN;SP1;PU800,800;CI400,90;PR;PD400,0;");
        QVERIFY2(circle.error.isEmpty(), qPrintable(circle.error));
        QCOMPARE(circle.paths.size(), 2);
        QCOMPARE(circle.paths[0].boundingRect(), QRectF(0, 0, 20, 20));
        QCOMPARE(QPointF(circle.paths[0].elementAt(0)),
                 QPointF(circle.paths[0].elementAt(circle.paths[0].elementCount() - 1)));
        // CI draws while pen-up, restores the centre, and leaves PA/PR untouched.
        QCOMPARE(QPointF(circle.paths[1].elementAt(0)), QPointF(10, 10));
        QCOMPARE(QPointF(circle.paths[1].elementAt(1)), QPointF(20, 10));
        const auto arc = PlotImport::hpgl("INSP1PU400,0PDAA0,0,90,90;AR0,-400,-90,90;");
        QVERIFY2(arc.error.isEmpty(), qPrintable(arc.error));
        QCOMPARE(arc.paths.size(), 1);
        QVERIFY(QLineF(QPointF(arc.paths[0].elementAt(0)), QPointF(10,0)).length() < 1e-10);
        QVERIFY(QLineF(QPointF(arc.paths[0].elementAt(1)), QPointF(0,10)).length() < 1e-10);
        QVERIFY(QLineF(QPointF(arc.paths[0].elementAt(2)), QPointF(10,0)).length() < 1e-10);
        const auto curve = PlotImport::hpgl("IN;SP1;PU400,400;PD;"
            "BR0,400,400,400,400,0,0,-400,400,-400,400,0;");
        QVERIFY2(curve.error.isEmpty(), qPrintable(curve.error));
        QCOMPARE(curve.paths.size(), 1);
        const auto &p = curve.paths[0];
        QCOMPARE(p.elementCount(), 7);
        QCOMPARE(p.elementAt(1).type, QPainterPath::CurveToElement);
        // Each BR triple is relative to the curve's start, not the prior control point.
        QCOMPARE(p.elementAt(3).x, 10.);
        QCOMPARE(p.elementAt(6).x, 20.);
        QCOMPARE(p.elementAt(0).y, p.elementAt(3).y);
        QCOMPARE(p.elementAt(3).y, p.elementAt(6).y);
        QVERIFY(p.elementAt(1).y > p.elementAt(0).y);
        QVERIFY(p.elementAt(4).y < p.elementAt(3).y);
        const auto abs = PlotImport::hpgl("IN;SP1;PD;BZ0,400,400,400,400,0;");
        QVERIFY(abs.error.isEmpty());
        QCOMPARE(abs.paths[0].elementCount(), 4);
    }
    void hpglImportIsAtomicAndUndoable() {
        Document d;
        d.newProject();
        QVERIFY(d.addPage("Import"));
        const auto source = d.projectBytes();
        const auto path = write("shape.PLT", "IN;SP1;PU0,0;PD800,0,800,400,0,0;SP0;");
        QVERIFY2(d.importFile(path), qPrintable(d.message()));
        QCOMPARE(d.count(), 1);
        QCOMPARE(d.items()[0].page, 1);
        QCOMPARE(d.items()[0].bounds, QRectF(0, 0, 20, 10));
        QCOMPARE(d.history().last().toMap()["label"].toString(), QString("Geometrie importieren"));
        const auto imported = d.projectBytes();
        d.undo();
        QCOMPARE(d.projectBytes(), source);
        d.redo();
        QCOMPARE(d.projectBytes(), imported);
        const QList<QByteArray> invalid = {
            "SC0,100,0,100;", "IP0,0,100;", "RO90;", "PM0;", "LBhello\003;",
            "PA1;", "PD1,,2;", "PD1,2,;", "PD1.2.3,4;", "PA1e3,0;", "IN1;",
            "AA0,0,90,0;", "CI-400;", "BZ1,2;", "SP1.5;", "PU80001,0;",
            QByteArray("\033%0B"), QByteArray("PA1,2\0;", 8)
        };
        for (const auto &bad : invalid) {
            const QByteArray bytes = "IN;SP1;PD400,0;" + bad;
            const auto parsed = PlotImport::hpgl(bytes);
            QVERIFY2(!parsed.error.isEmpty(), bytes.constData());
            QVERIFY(parsed.paths.isEmpty());
            const auto revision = d.historyRevision();
            const auto history = d.history();
            QVERIFY2(!d.importFile(write("invalid.hpgl", bytes)), bytes.constData());
            QCOMPARE(d.projectBytes(), imported);
            QCOMPARE(d.historyRevision(), revision);
            QCOMPARE(d.history(), history);
        }
        QVERIFY(!PlotImport::hpgl("IN;PU0,0;PD400,0;").error.isEmpty());
    }
    void hpglResourceBudgets() {
        QVERIFY(!PlotImport::hpgl(QByteArray(16 * 1024 * 1024 + 1, ' ')).error.isEmpty());
        QByteArray many = "IN;SP1;";
        for (int i = 0; i < 2001; ++i)
            many += "PU0,0;PD400,0;";
        QVERIFY(PlotImport::hpgl(many).error.contains("Zu viele HPGL-Pfade"));
        // Work is bounded even for pen-up arcs that create no path elements.
        const auto hidden = PlotImport::hpgl("IN;PU400,0;AA0,0,32767,0.5;"
            "AA0,0,32767,0.5;AA0,0,32767,0.5;AA0,0,32767,0.5;");
        QVERIFY2(hidden.error.contains("Elementlimit"), qPrintable(hidden.error));
        QVERIFY(hidden.paths.isEmpty());
        many = "IN;SP1;PD;";
        for (int i = 0; i < 100001; ++i)
            many += i % 2 ? "PA400,0;" : "PA0,0;";
        QVERIFY(!PlotImport::hpgl(many).error.isEmpty());
    }
    void hpglRealInkscapeExportAndRoundtrip() {
        const auto fixture = QFINDTESTDATA("fixtures/hpgl/inkscape-l.hpgl");
        QVERIFY(!fixture.isEmpty());
        QFile file(fixture);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto result = PlotImport::hpgl(file.readAll());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.paths.size(), 1);
        const auto &p = result.paths[0];
        QCOMPARE(p.boundingRect(), QRectF(0, 0, 20, 10));
        QCOMPARE(p.elementCount(), 5);
        QCOMPARE(QPointF(p.elementAt(0)), QPointF(0, 0));
        QCOMPARE(QPointF(p.elementAt(1)), QPointF(20, 0));
        QCOMPARE(QPointF(p.elementAt(2)), QPointF(20, 10));
        QCOMPARE(QPointF(p.elementAt(3)), QPointF(5, 10));
        Document d;
        d.newProject();
        QVERIFY2(d.importFile(QUrl::fromLocalFile(fixture)), qPrintable(d.message()));
        const auto code = d.job().gcode();
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        Document restored;
        QVERIFY(restored.restoreBytes(d.projectBytes()));
        QCOMPARE(restored.items()[0].path, d.items()[0].path);
        QCOMPARE(restored.job().gcode(), code);
    }
    void operationPassesAndPages() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("width", 10.);
        d.setProperty("height", 10.);
        int op = d.addOperation("Fine", "Gravur");
        QCOMPARE(op, 2);
        d.setLayerSetting(op, "interval", 1.);
        d.assignOperations({op, 1});
        auto j = d.job();
        QVERIFY2(j.valid(), qPrintable(j.errors.join(";")));
        bool fill = false, cut = false;
        for (auto m : j.moves) {
            fill |= m.layer == op;
            cut |= m.layer == 1;
        }
        QVERIFY(fill && cut);
        auto bytes = d.projectBytes();
        Document restored;
        QVERIFY(restored.restoreBytes(bytes));
        QCOMPARE(restored.layers().size(), 3);
        QCOMPARE(restored.items()[0].extraOperations, QList<int>{1});
        QVERIFY(d.addPage("Back"));
        QVERIFY(!d.job().valid());
        d.addShape("ellipse");
        QCOMPARE(d.items()[1].page, 1);
        QVERIFY(d.job().valid());
        d.activatePage(0);
        QCOMPARE(d.activePage(), 0);
        QCOMPARE(d.job().gcode(), j.gcode());
        d.undo();
        QCOMPARE(d.pages().size(), 2);
    }
    void transformationsAndNodes() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("width", 20.);
        d.setProperty("height", 10.);
        auto center = d.selectionBounds().center();
        QVERIFY(d.command("rotate", {{"value", 90.}}));
        QCOMPARE(d.items()[0].kind, "path");
        QVERIFY(std::abs(d.selectionBounds().width() - 10) < 1e-6);
        QVERIFY(QLineF(center, d.selectionBounds().center()).length() < 1e-6);
        QVERIFY(!d.nodes().isEmpty());
        auto node = d.nodes().first().toMap();
        QVERIFY(d.setNode(0, node["x"].toDouble() + 1, node["y"].toDouble()));
        d.undo();
        auto before = d.projectBytes();
        QVERIFY(d.command("mirrorX"));
        QVERIFY(d.command("mirrorX"));
        QVERIFY(d.job().valid());
        d.undo();
        d.undo();
        QCOMPARE(d.projectBytes(), before);
    }
    void booleansAndAlignment() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 10.);
        d.setProperty("y", 10.);
        d.setProperty("width", 20.);
        d.setProperty("height", 20.);
        d.addShape("rect");
        d.setProperty("x", 20.);
        d.setProperty("y", 10.);
        d.setProperty("width", 20.);
        d.setProperty("height", 20.);
        d.toggleSelection(0);
        QVERIFY(d.command("intersection"));
        QCOMPARE(d.count(), 1);
        QVERIFY(std::abs(d.selectionBounds().width() - 10) < 1e-6);
        d.undo();
        QCOMPARE(d.count(), 2);
        QVERIFY(d.command("alignLeft"));
        QCOMPARE(d.items()[0].bounds.x(), d.items()[1].bounds.x());
        d.undo();
        QVERIFY(d.command("union"));
        QVERIFY(d.job().valid());
    }
    void recoveryAndBackup() {
        Document d;
        d.newProject();
        Recovery recovery(&d, temp.filePath("recovery"));
        d.addShape("rect");
        d.setProperty("width", 20.);
        QVERIFY(recovery.save());
        QVERIFY(recovery.available());
        auto bytes = d.projectBytes();
        d.setProperty("width", 30.);
        QVERIFY(recovery.restore());
        QCOMPARE(d.items()[0].bounds.width(), 20.);
        auto url = QUrl::fromLocalFile(temp.filePath("saved.blazer"));
        QVERIFY(d.save(url));
        d.setProperty("width", 25.);
        QVERIFY(d.save(url));
        QVERIFY(QFile::exists(url.toLocalFile() + ".bak"));
        QFile backup(url.toLocalFile() + ".bak");
        QVERIFY(backup.open(QIODevice::ReadOnly));
        Document restored;
        QVERIFY(restored.restoreBytes(backup.readAll()));
        QCOMPARE(restored.items()[0].bounds.width(), 20.);
        recovery.clear();
        QVERIFY(!recovery.available());
    }
    void scanAndCutFeatures() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 20.);
        d.setProperty("y", 20.);
        d.setProperty("width", 10.);
        d.setProperty("height", 10.);
        d.setLayer(0);
        d.setLayerSetting(0, "interval", 1.);
        double base = d.job().workLength;
        d.setLayerSetting(0, "crosshatch", true);
        QVERIFY(d.job().valid());
        QVERIFY(std::abs(d.job().workLength - 2 * base) < 1e-6);
        d.setLayerSetting(0, "angle", 45.);
        d.setLayerSetting(0, "overscan", 1.);
        QVERIFY(d.job().valid());
        QVERIFY(d.job().seconds >= d.job().minimumSeconds);
        d.setLayer(1);
        d.setLayerSetting(1, "tabs", 4);
        d.setLayerSetting(1, "tabWidth", 1.);
        QVERIFY(d.job().valid());
        QVERIFY(std::abs(d.job().workLength - 36) < 1e-6);
        d.setLayerSetting(1, "tabs", 0);
        d.setLayerSetting(1, "kerf", .2);
        QVERIFY(d.job().valid());
        QVERIFY(d.job().workLength > 40);
        d.setProperty("x", 0.);
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().moves.isEmpty());
    }
    void imageModesAndTrace() {
        Document d;
        d.newProject();
        QVERIFY(d.importImage(bitmap()));
        QCOMPARE(d.items()[0].kind, "image");
        QVERIFY(d.command("imageSettings", {{"imageMode", "grayscale"}, {"sharpness", 1.}}));
        QVERIFY(d.job().valid());
        QSet<double> powers;
        for (auto move : d.job().moves)
            if (!move.rapid)
                powers.insert(move.power);
        QVERIFY(powers.size() > 3);
        auto url = QUrl::fromLocalFile(temp.filePath("raster.blazer"));
        QVERIFY(d.save(url));
        Document restored;
        QVERIFY(restored.open(url));
        QCOMPARE(restored.job().gcode(), d.job().gcode());
        QVERIFY(!d.command("imageSettings", {{"cropW", 10000}}));
        QVERIFY(d.command("imageSettings", {{"imageMode", "threshold"}, {"ellipseMask", true}}));
        QVERIFY(d.command("trace"));
        QCOMPARE(d.items()[0].kind, "path");
        QVERIFY(d.job().valid());
        d.undo();
        QCOMPARE(d.items()[0].kind, "image");
    }
    void sixteenBitAndCleanup() {
        QImage image(2, 1, QImage::Format_RGBA64);
        image.setPixelColor(0, 0, QColor::fromRgba64(32767, 32767, 32767, 65535));
        image.setPixelColor(1, 0, QColor::fromRgba64(32768, 32768, 32768, 65535));
        auto path = temp.filePath("16bit.png");
        QVERIFY(image.save(path));
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(QUrl::fromLocalFile(path)));
        QVERIFY(d.items()[0].reliefAsset->image.depth() > 32);
        ReliefSettings p;
        p.passes = 256;
        QCOMPARE(p.count(d.items()[0].reliefAsset->image.pixelColor(0, 0).rgba64()), 129);
        QCOMPARE(p.count(d.items()[0].reliefAsset->image.pixelColor(1, 0).rgba64()), 128);
        d.setReliefSetting("passes", 4);
        d.setReliefSetting("cleanupEvery", 1);
        d.setReliefSetting("crosshatch", true);
        QVERIFY(d.job().valid());
        bool cleaning = false;
        for (auto m : d.job().moves)
            cleaning |= m.slice < 0;
        QVERIFY(cleaning);
        QVERIFY(d.job().gcode().join('\n').contains("cleanup after"));
    }
    void qrAndProduction() {
        Document d;
        d.newProject();
        QVERIFY(d.command("qr", {{"text", "https://example.org"}}));
        QVERIFY(d.job().valid());
        if (qEnvironmentVariableIsSet("BLAZER_QR_CHECK")) {
            QImage image(600, 600, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::black);
            painter.scale(600, 600);
            painter.drawPath(d.items()[0].path);
            painter.end();
            QVERIFY(image.save(qEnvironmentVariable("BLAZER_QR_CHECK")));
        }
        QVERIFY(d.command("duplicateGrid", {{"copies", 4}, {"columns", 2}, {"gap", 2}}));
        QCOMPARE(d.count(), 4);
        d.select(0);
        for (int i = 1; i < 4; ++i)
            d.toggleSelection(i);
        QVERIFY(d.command("nest", {{"gap", 2}}));
        QVERIFY(d.job().valid());
        d.newProject();
        d.addShape("text");
        d.setProperty("text", "{name}-{number}");
        auto csv = write("batch.csv", "name\n\"Alice, A\"\nBob\n");
        QDir().mkpath(temp.filePath("jobs"));
        auto before = d.projectBytes();
        QVERIFY(d.batch(csv, QUrl::fromLocalFile(temp.filePath("jobs")), ""));
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(QFile::exists(temp.filePath("jobs/job-0002.gcode")));
        QVERIFY(!d.batch(csv, QUrl::fromLocalFile(temp.filePath("jobs")), ""));
    }
    void deletingPageKeepsUnrelatedOutput_data() {
        QTest::addColumn<int>("active");
        QTest::addColumn<int>("removed");
        QTest::addColumn<int>("expected");
        QTest::addColumn<QString>("name");
        QTest::newRow("after-active") << 0 << 2 << 0 << QString("A");
        QTest::newRow("before-active") << 2 << 0 << 1 << QString("C");
        QTest::newRow("immediately-before-active") << 1 << 0 << 0 << QString("B");
        QTest::newRow("immediately-after-active") << 0 << 1 << 0 << QString("A");
        QTest::newRow("active-middle") << 1 << 1 << 1 << QString("C");
        QTest::newRow("active-last") << 2 << 2 << 1 << QString("B");
    }
    void deletingPageKeepsUnrelatedOutput() {
        QFETCH(int, active);
        QFETCH(int, removed);
        QFETCH(int, expected);
        QFETCH(QString, name);
        Document d;
        d.newProject();
        QVERIFY(d.renamePage(0, "A"));
        for (int i = 0; i < 3; ++i) {
            if (i)
                QVERIFY(d.addPage(i == 1 ? "B" : "C"));
            d.addShape("rect");
            d.setProperty("width", 10. * (i + 1));
        }
        d.activatePage(active);
        QVERIFY(d.save(QUrl::fromLocalFile(temp.filePath("page-delete.blazer"))));
        const auto before = d.projectBytes();
        const auto gcode = d.job().gcode();
        QVERIFY(d.removePage(removed));
        QCOMPARE(d.activePage(), expected);
        QCOMPARE(d.pages()[d.activePage()], name);
        if (removed != active)
            QCOMPARE(d.job().gcode(), gcode);
        QVERIFY(d.dirty());
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(!d.dirty());
        d.redo();
        QCOMPARE(d.pages()[d.activePage()], name);
    }
    void selectionCopyToAnotherPageKeepsIndependentGroups() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.addShape("ellipse");
        d.toggleSelection(0);
        d.groupSelection();
        const auto sourceGroup = d.items()[0].groupId;
        QVERIFY(!sourceGroup.isEmpty());
        QVERIFY(d.addPage("Ziel"));
        d.activatePage(0);
        d.selectGroup(0);
        QVERIFY(d.command("copyToPage", {{"page", 1}}));
        QCOMPARE(d.items().size(), 4);
        QVERIFY(!d.items()[2].groupId.isEmpty());
        QCOMPARE(d.items()[2].groupId, d.items()[3].groupId);
        QVERIFY(d.items()[2].groupId != sourceGroup);
        QCOMPARE(d.items()[2].groupName, d.items()[0].groupName);
        QCOMPARE(d.activePage(), 0);
        d.select(0);
        QVERIFY(d.command("copyToPage", {{"page", 1}}));
        QVERIFY(d.items().last().groupId.isEmpty());
        QVERIFY(d.items().last().groupName.isEmpty());
        d.activatePage(1);
        d.selectGroup(2);
        QCOMPARE(d.selectedRows(), QVariantList({2, 3}));
        d.activatePage(0);
        d.selectGroup(0);
        QVERIFY(d.command("copyToPage", {{"page", 0}}));
        d.selectGroup(5);
        QCOMPARE(d.selectedRows(), QVariantList({5, 6}));
        QVERIFY(d.items()[5].groupId != sourceGroup);
    }
    void fullPageCopyPreservesContentAndOutputIndependently() {
        Document d;
        d.newProject();
        QVERIFY(d.setMetadata("Mehrere Blätter", "Fokus manuell; gleiche Vorlage"));
        d.addShape("rect");
        d.addShape("ellipse");
        d.toggleSelection(0);
        d.groupSelection();
        const auto imageFile = bitmap();
        QVERIFY(d.importImage(imageFile));
        d.toggleVisible(2);
        const auto svgFile = write("sheet-source.svg", "<svg xmlns='http://www.w3.org/2000/svg' "
                                   "width='10mm' height='8mm'><rect width='10' height='8'/></svg>");
        QVERIFY(d.importSvg(svgFile));
        d.addShape("text");
        d.setProperty("text", "Seite A");
        const int operation = d.addOperation("Weitere Gravur", "Gravur");
        d.assignOperations({0, operation});
        d.setProperty("power", 17.);
        QVERIFY(d.importRelief(imageFile));
        d.toggleVisible(5);
        QVERIFY(d.addPage("Anderes Blatt"));
        d.addShape("rect");
        d.activatePage(0);
        const auto sourcePath = QUrl::fromLocalFile(temp.filePath("sheets-source.blazer"));
        QVERIFY(d.save(sourcePath));
        const auto before = d.projectBytes();
        const auto source = d.items();
        const auto operations = d.layers();
        const auto gcode = d.job().gcode();
        QVERIFY(d.job().valid());
        const auto sourceSvg = QUrl::fromLocalFile(temp.filePath("sheet-export-source.svg"));
        const auto otherSvg = QUrl::fromLocalFile(temp.filePath("sheet-export-other.svg"));
        const auto copySvg = QUrl::fromLocalFile(temp.filePath("sheet-export-copy.svg"));
        auto svgBytes = [](const QUrl &file) {
            QFile input(file.toLocalFile());
            return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
        };
        QVERIFY(d.exportSvg(sourceSvg));
        QVERIFY(svgBytes(sourceSvg).contains("<path"));
        d.activatePage(1);
        QVERIFY(d.exportSvg(otherSvg));
        QVERIFY(svgBytes(otherSvg) != svgBytes(sourceSvg));
        d.activatePage(0);
        QVERIFY(d.copyPage(0, " Rückseite "));
        QCOMPARE(d.pages(), QStringList({"Blatt 01", "Anderes Blatt", "Rückseite"}));
        QCOMPARE(d.activePage(), 2);
        QCOMPARE(d.selected(), -1);
        QCOMPARE(d.fileUrl(), sourcePath);
        QCOMPARE(d.layers(), operations);
        const int copiesStart = source.size();
        const int copiedCount = source.size() - 1;
        QCOMPARE(d.items().size(), copiesStart + copiedCount);
        QCOMPARE(d.job().gcode(), gcode);
        QVERIFY(d.exportSvg(copySvg));
        QCOMPARE(svgBytes(copySvg), svgBytes(sourceSvg));
        QVERIFY(d.dirty());
        QSet<QString> ids;
        for (const auto &item : d.items()) {
            QVERIFY(!ids.contains(item.id));
            ids.insert(item.id);
        }
        for (int i = 0; i < copiedCount; ++i) {
            const auto &copy = d.items()[source.size() + i];
            auto comparable = copy;
            comparable.id = source[i].id;
            comparable.page = source[i].page;
            if (!source[i].groupId.isEmpty()) {
                QVERIFY(copy.groupId != source[i].groupId);
                comparable.groupId = source[i].groupId;
            }
            QVERIFY(comparable == source[i]);
        }
        QCOMPARE(d.items()[copiesStart].groupId, d.items()[copiesStart + 1].groupId);
        QVERIFY(!d.items()[copiesStart + 2].visible);
        QCOMPARE(d.items()[copiesStart + 4].extraOperations, QList<int>{operation});
        QVERIFY(d.items()[copiesStart + 5].reliefAsset);
        // Both embedded formats survive removal of their original files.
        QVERIFY(QFile::remove(svgFile.toLocalFile()));
        QVERIFY(QFile::remove(imageFile.toLocalFile()));
        Document reopened;
        QVERIFY(reopened.restoreBytes(d.projectBytes()));
        const auto output = QUrl::fromLocalFile(temp.filePath("sheets-copy.blazer"));
        QVERIFY(reopened.save(output));
        Document loaded;
        QVERIFY(loaded.open(output));
        QCOMPARE(loaded.projectBytes(), d.projectBytes());
        QCOMPARE(loaded.job().gcode(), gcode);
        {
            Recovery recovery(&d, temp.filePath("sheets-recovery"));
            QVERIFY(recovery.save());
        }
        Document recovered;
        Recovery recovery(&recovered, temp.filePath("sheets-recovery"));
        QVERIFY(recovery.restore());
        QCOMPARE(recovered.activePage(), 2);
        QCOMPARE(recovered.job().gcode(), gcode);
        d.select(copiesStart + 4);
        const auto beforeEdit = d.projectBytes();
        const auto width = d.items()[copiesStart + 4].bounds.width();
        d.setProperty("width", width + 2);
        QVERIFY(d.items()[4] == source[4]);
        QVERIFY(d.items()[copiesStart + 4].bounds.width() != source[4].bounds.width());
        d.undo();
        QCOMPARE(d.projectBytes(), beforeEdit);
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(!d.dirty());
        d.redo();
        QCOMPARE(d.activePage(), 2);
        QCOMPARE(d.job().gcode(), gcode);
    }
    void pageCopyLimitsAndRejectedNamesAreAtomic() {
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto before = d.projectBytes();
        for (const QString &name : {QString(), QString("  "), QString("A\nB"),
                                    QString("A\rB"), QString(201, 'x')}) {
            QVERIFY(!d.addPage(name));
            QVERIFY(!d.renamePage(0, name));
            QVERIFY(!d.copyPage(0, name));
            QCOMPARE(d.projectBytes(), before);
        }
        QVERIFY(!d.copyPage(-1, "X"));
        QVERIFY(!d.copyPage(1, "X"));
        QCOMPARE(d.projectBytes(), before);
        // Rejections must not insert a no-op history entry.
        d.undo();
        QCOMPARE(d.count(), 0);
        d.newProject();
        for (int i = 1; i < 100; ++i)
            QVERIFY(d.addPage(QString("Blatt %1").arg(i + 1)));
        const auto atLimit = d.projectBytes();
        QVERIFY(!d.addPage("101"));
        QVERIFY(!d.copyPage(0, "101"));
        QCOMPARE(d.projectBytes(), atLimit);
        d.undo();
        QCOMPARE(d.pages().size(), 99);
        // Empty sheets are real, undoable copies too.
        QVERIFY(d.copyPage(0, QString(200, 'x')));
        QCOMPARE(d.count(), 0);
        QCOMPARE(d.activePage(), 99);
        d.newProject();
        d.addShape("rect");
        d.setProperty("width", 1.);
        d.setProperty("height", 1.);
        QVERIFY(d.command("duplicateGrid", {{"copies", 1000}, {"columns", 32}, {"gap", 0}}));
        d.addShape("ellipse");
        QCOMPARE(d.count(), 1001);
        const auto objects = d.projectBytes();
        QVERIFY(!d.copyPage(0, "Zu viele Objekte"));
        QCOMPARE(d.projectBytes(), objects);
        d.undo();
        QCOMPARE(d.count(), 1000);
        d.undo();
        QCOMPARE(d.count(), 1);
        // Embedded-byte limits count references on both sheets, even when
        // the in-memory asset is immutable and shared.
        d.newProject();
        const auto largeSvg = write("large-sheet.svg", "<svg xmlns='http://www.w3.org/2000/svg' "
                                    "width='10mm' height='10mm'><!--" +
                                    QByteArray(6 * 1024 * 1024, 'x') +
                                    "--><rect width='10' height='10'/></svg>");
        QVERIFY(d.importSvg(largeSvg));
        for (int i = 0; i < 3; ++i)
            d.duplicateSelected();
        QCOMPARE(d.count(), 4);
        const auto originalItems = d.items();
        QVERIFY(!d.copyPage(0, "Zu viele Einbettungen"));
        QCOMPARE(d.pages().size(), 1);
        QVERIFY(d.items() == originalItems);
        d.undo();
        QCOMPARE(d.count(), 3);
    }
    void operationManagementAndMultiSheet() {
        Document d;
        d.newProject();
        d.addShape("rect");
        int a = d.addOperation("A", "Gravur"), b = d.addOperation("B", "Schnitt");
        d.assignOperations({a, b});
        QVERIFY(d.manageOperation(b, "up"));
        QCOMPARE(d.items()[0].layer, 3);
        QCOMPARE(d.items()[0].extraOperations, QList<int>{2});
        QVERIFY(d.manageOperation(3, "delete"));
        QCOMPARE(d.items()[0].layer, 0);
        QVERIFY(d.items()[0].extraOperations.contains(2));
        d.undo();
        QCOMPARE(d.items()[0].layer, 3);
        d.newProject();
        d.addShape("rect");
        d.setProperty("width", 180.);
        d.setProperty("height", 140.);
        QVERIFY(d.command("duplicateGrid", {{"copies", 2}, {"columns", 2}, {"gap", 2}}));
        d.select(0);
        d.toggleSelection(1);
        QVERIFY2(d.command("nest", {{"gap", 2}, {"multipage", true}}), qPrintable(d.message()));
        QCOMPARE(d.pages().size(), 2);
        QVERIFY(d.job().valid());
        d.activatePage(1);
        QVERIFY(d.job().valid());
        Document restored;
        QVERIFY(restored.restoreBytes(d.projectBytes()));
        QCOMPARE(restored.activePage(), 1);
        QVERIFY(d.removePage(1));
        QCOMPARE(d.pages().size(), 1);
        d.undo();
        QCOMPARE(d.pages().size(), 2);
        Document grouped;
        grouped.newProject();
        grouped.addShape("rect");
        grouped.setProperty("width", 10.);
        grouped.setProperty("height", 10.);
        grouped.duplicateSelected();
        grouped.toggleSelection(0);
        grouped.groupSelection();
        auto delta = grouped.items()[1].bounds.topLeft() - grouped.items()[0].bounds.topLeft();
        auto id = grouped.items()[0].groupId;
        QVERIFY(grouped.command("nest", {{"gap", 0}}));
        QCOMPARE(grouped.items()[0].groupId, id);
        QCOMPARE(grouped.items()[1].groupId, id);
        QVERIFY(
            QLineF(delta, grouped.items()[1].bounds.topLeft() - grouped.items()[0].bounds.topLeft())
                .length() < 1e-6);
    }
    void registrationAndInvalidProjects() {
        Registration registration;
        QVERIFY(registration.load(bitmap()));
        QVERIFY(
            !registration.calibrate({0, 0, 0, 0, 0, 0, 0, 0}, {0, 150, 200, 150, 200, 0, 0, 0}));
        QVERIFY(
            registration.calibrate({0, 0, 20, 0, 20, 10, 0, 10}, {0, 150, 200, 150, 200, 0, 0, 0}));
        QCOMPARE(registration.transform().map(QPointF(10, 5)), QPointF(100, 75));
        QVERIFY(registration.visible());
        registration.setVisible(false);
        QVERIFY(!registration.visible());
        Document d;
        d.newProject();
        d.addShape("rect");
        auto json = QJsonDocument::fromJson(d.projectBytes()).object();
        auto ops = json["layers"].toArray();
        auto op = ops[1].toObject();
        op["tabs"] = 1000000000;
        ops[1] = op;
        json["layers"] = ops;
        auto old = d.projectBytes();
        QVERIFY(!d.restoreBytes(QJsonDocument(json).toJson()));
        QCOMPARE(d.projectBytes(), old);
        auto legacy = QJsonDocument::fromJson(old).object();
        legacy["version"] = 5;
        Document restored;
        QVERIFY(restored.restoreBytes(QJsonDocument(legacy).toJson()));
        QCOMPARE(restored.count(), 1);
    }
    void pdfLightBurnAndVectorClip() {
        Document d;
        d.newProject();
        auto lb = write(
            "geometry.lbrn2",
            "<LightBurnProject MirrorX=\"False\" MirrorY=\"True\"><Shape Type=\"Group\"><XForm>1 0 "
            "0 1 10 10</XForm><Children><Shape Type=\"Path\"><XForm>1 0 0 1 0 "
            "0</XForm><VertList>V0 0c0x1c1x1V10 0c0x1c1x1V10 10c0x1c1x1</VertList><PrimList>L0 1L1 "
            "2L2 0</PrimList></Shape><Shape Type=\"Rect\" W=\"5\" H=\"5\"><XForm>1 0 0 1 20 "
            "20</XForm></Shape></Children></Shape></LightBurnProject>");
        QVERIFY2(d.importFile(lb), qPrintable(d.message()));
        QCOMPARE(d.history().last().toMap()["label"].toString(),QString("Geometrie importieren"));
        QCOMPARE(d.count(), 2);
        QVERIFY(d.items()[0].bounds.x() >= 10);
        auto group = d.items()[0].groupId;
        QVERIFY(!group.isEmpty());
        QCOMPARE(d.items()[1].groupId, group);
        Document restored;
        QVERIFY(restored.restoreBytes(d.projectBytes()));
        d.newProject();
        auto svg =
            write("clip.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20mm\" "
                              "height=\"20mm\" viewBox=\"0 0 20 20\"><defs><clipPath "
                              "id=\"c\"><rect width=\"10\" height=\"20\"/></clipPath></defs><rect "
                              "width=\"20\" height=\"20\" clip-path=\"url(#c)\"/></svg>");
        QVERIFY(d.importSvg(svg));
        d.setLayerSetting(0, "interval", 1.);
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QCOMPARE(d.job().workLength, 200.);
        if (!QStandardPaths::findExecutable("inkscape").isEmpty()) {
            auto path = temp.filePath("drawing.pdf");
            {
                QPdfWriter pdf(path);
                pdf.setPageSize(QPageSize(QSizeF(40, 40), QPageSize::Millimeter));
                pdf.setResolution(96);
                QPainter painter(&pdf);
                painter.drawRect(20, 20, 60, 40);
            }
            d.newProject();
            QVERIFY2(d.importFile(QUrl::fromLocalFile(path)), qPrintable(d.message()));
            QVERIFY(d.count() > 0);
        }
    }
    void vectorImportsAndExport() {
        Document d;
        d.newProject();
        auto dxf = write("line.dxf", "0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n10\n20\n10\n11\n20\n21"
                                     "\n20\n0\nENDSEC\n0\nEOF\n");
        QVERIFY(d.importFile(dxf));
        QCOMPARE(d.history().last().toMap()["label"].toString(),QString("Geometrie importieren"));
        QCOMPARE(d.count(), 1);
        QVERIFY(d.job().valid());
        QVERIFY(d.exportSvg(QUrl::fromLocalFile(temp.filePath("export.svg"))));
        auto bad =
            write("unsupported.dxf", "0\nSECTION\n2\nENTITIES\n0\nSPLINE\n0\nENDSEC\n0\nEOF\n");
        QVERIFY(!d.importFile(bad));
        QCOMPARE(d.count(), 1);
        auto model = write("wedge.obj", "v 0 0 0\nv 10 0 0\nv 0 10 10\nf 1 2 3\n");
        QVERIFY(d.importModel(model));
        QCOMPARE(d.items().last().kind, "relief");
        QVERIFY(d.items().last().reliefAsset->image.depth() > 32);
    }
};
QTEST_MAIN(ExpansionTest)
#include "expansion_test.moc"
