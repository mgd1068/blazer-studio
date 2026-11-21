#include "barcode.h"
#include "document.h"
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <cmath>

class BarcodeTest : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    void decode(const QImage &source, const QByteArray &expected, bool absent = false) {
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty()) {
            QVERIFY2(!qEnvironmentVariableIsSet("BLAZER_REQUIRE_BARCODE_DECODER"),
                     "Required independent barcode acceptance: python3 missing");
            QSKIP("Python3 unavailable: independent ZBar acceptance not run");
        }
        const auto image = source.convertToFormat(QImage::Format_Grayscale8);
        QFile file(temporary.filePath("barcode.pgm"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("P5\n" + QByteArray::number(image.width()) + " " + QByteArray::number(image.height()) + "\n255\n");
        for (int y = 0; y < image.height(); ++y)
            QCOMPARE(file.write(reinterpret_cast<const char *>(image.constScanLine(y)), image.width()), image.width());
        file.close();
        QProcess decoder;
        decoder.start(python, {QString(BLAZER_TEST_SOURCE_DIR) + "/tests/barcode_decode.py", file.fileName(),
                               absent ? "-" : QString::fromLatin1(expected.toHex())});
        QVERIFY(decoder.waitForFinished(10000));
        if (decoder.exitCode() == 77) {
            QVERIFY2(!qEnvironmentVariableIsSet("BLAZER_REQUIRE_BARCODE_DECODER"),
                     "Required independent barcode acceptance: ZBar missing");
            QSKIP("Independent ZBar decoder unavailable: image acceptance not run");
        }
        QVERIFY2(decoder.exitCode() == 0, decoder.readAllStandardError().constData());
    }
    QImage render(const QPainterPath &path, int modules) {
        QImage image(modules * 4, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.scale(image.width(), image.height());
        painter.drawPath(path);
        return image;
    }
  private slots:
    void boundsQuietZonesAndIndependentDecoder() {
        QString ascii;
        for (int c = 32; c <= 126; ++c)
            ascii += QChar(c);
        for (const auto &text : {QString("A"), QString("AB"), QString("0123456789"),
                                 QString("Blazer-2026 / xYz!"), ascii, QString(128, '~')}) {
            const auto code = Barcode::code128B(text);
            QVERIFY2(code.valid(), qPrintable(code.error));
            QCOMPARE(code.modules, 55 + 11 * text.size());
            QVERIFY(code.path.elementCount() <= 2000);
            QVERIFY(std::abs(code.path.boundingRect().left() * code.modules - 10) < 1e-8);
            QVERIFY(std::abs((1 - code.path.boundingRect().right()) * code.modules - 10) < 1e-8);
            // Every bar boundary and width lies on the same integral module grid.
            for (int i = 0; i < code.path.elementCount(); ++i) {
                const double x = code.path.elementAt(i).x * code.modules;
                QVERIFY(std::abs(x - std::round(x)) < 1e-8);
            }
            decode(render(code.path, code.modules), text.toLatin1());
        }
    }
    void rejectedInputIsBoundedAndAtomic() {
        for (const auto &text : {QString(), QString(129, 'a'), QString("ä"), QString("a\nb"),
                                 QString(QChar(0)), QString(QChar(127)), QString::fromUcs4(U"😀")})
            QVERIFY(!Barcode::code128B(text).valid());
        Document d;
        d.newProject();
        const auto bytes = d.projectBytes();
        const auto revision = d.historyRevision();
        const QList<QVariantMap> bad{
            {}, {{"text", 123}}, {{"text", "a\nb"}}, {{"text", "valid"}, {"moduleWidth", true}},
            {{"text", "valid"}, {"moduleWidth", "0.3"}}, {{"text", "valid"}, {"moduleWidth", .09}},
            {{"text", "valid"}, {"moduleWidth", 2.01}}, {{"text", "valid"}, {"height", .9}},
            {{"text", "valid"}, {"height", std::numeric_limits<double>::infinity()}},
            {{"text", "valid"}, {"x", -1.}}, {{"text", "valid"}, {"x", 195.}},
            {{"text", "valid"}, {"y", std::numeric_limits<double>::quiet_NaN()}},
            {{"text", QString(128, 'a')}}};
        for (const auto &args : bad) {
            QVERIFY(!d.command("barcode", args));
            QCOMPARE(d.projectBytes(), bytes);
            QCOMPARE(d.historyRevision(), revision);
        }
        QVERIFY(d.command("barcode", {{"text", QString(128, 'a')}, {"moduleWidth", .1}}));
        QVERIFY(d.job().valid());
    }
    void nestingReservesCompleteCodeBounds() {
        Document d;
        d.newProject();
        QVERIFY(d.command("barcode", {{"text", "AB"}}));
        const auto original = d.items().first().bounds;
        QVERIFY(d.command("duplicateGrid", {{"copies", 2}, {"columns", 2}, {"gap", 2.}}));
        QCOMPARE(d.count(), 2);
        d.select(0);
        d.toggleSelection(1);
        QVERIFY(d.command("nest", {{"gap", 0.}}));
        for (const auto &item : d.items()) {
            QVERIFY(item.style.value("barcode").toBool());
            QVERIFY(std::abs(item.bounds.width() * item.bounds.height() -
                             original.width() * original.height()) < 1e-6);
        }
        QVERIFY(!d.items()[0].bounds.intersects(d.items()[1].bounds));
    }
    void documentRoundtripUndoAndSvgExportRemainDecodable() {
        Document d;
        d.newProject();
        const auto empty = d.projectBytes();
        const QString text = "Batch 2026-11 / A~";
        QVERIFY(d.command("barcode", {{"text", text}}));
        const auto a = d.items().first();
        QCOMPARE(a.text, text);
        QCOMPARE(a.bounds.height(), 15.);
        QVERIFY(a.style.value("barcode").toBool());
        QVERIFY(d.job().valid());
        Document restored;
        QVERIFY(restored.restoreBytes(d.projectBytes()));
        QCOMPARE(restored.items().first().path, a.path);
        QCOMPARE(restored.items().first().bounds, a.bounds);
        QCOMPARE(restored.job().gcode(), d.job().gcode());
        decode(render(restored.items().first().path, Barcode::code128B(text).modules), text.toLatin1());
        d.undo();
        QCOMPARE(d.projectBytes(), empty);
        d.redo();
        QCOMPARE(d.items().first().path, a.path);
        const auto svg = QUrl::fromLocalFile(temporary.filePath("barcode.svg"));
        QVERIFY(d.exportSvg(svg));
        QSvgRenderer renderer(svg.toLocalFile());
        QVERIFY(renderer.isValid());
        renderer.setViewBox(QRectF(a.bounds.x(), d.bedHeight() - a.bounds.y() - a.bounds.height(),
                                  a.bounds.width(), a.bounds.height()));
        QImage image(Barcode::code128B(text).modules * 4, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        decode(image, text.toLatin1());
        // A blank image must never be reported as a successful decode.
        image.fill(Qt::white);
        decode(image, {}, true);
    }
};
QTEST_MAIN(BarcodeTest)
#include "barcode_test.moc"
