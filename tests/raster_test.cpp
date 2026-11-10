#include "document.h"
#include "imageprocessing.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
class RasterTest : public QObject {
    Q_OBJECT
  private slots:
    void nativeResolutionRoundtrip() {
        QTemporaryDir dir;
        QImage input(1200, 900, QImage::Format_ARGB32);
        input.fill(Qt::white);
        for (int y = 0; y < 900; ++y)
            input.setPixel(601, y, qRgb(0, 0, 0));
        auto file = dir.filePath("fine.png");
        QVERIFY(input.save(file));
        Document d;
        d.newProject();
        QVERIFY(d.importImage(QUrl::fromLocalFile(file)));
        auto &a = d.items()[0];
        QCOMPARE(a.reliefAsset->image.size(), input.size());
        QCOMPARE(a.reliefAsset->image.pixelColor(601, 450), QColor(Qt::black));
        QCOMPARE(a.bounds.width(), 100.);
        QCOMPARE(a.bounds.height(), 75.);
        d.setLayerSetting(0, "interval", 100. / 1200);
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        auto project = QUrl::fromLocalFile(dir.filePath("native.blazer"));
        QVERIFY(d.save(project));
        Document restored;
        QVERIFY(restored.open(project));
        QCOMPARE(restored.items()[0].reliefAsset->image, input);
        QCOMPARE(restored.job().gcode(), d.job().gcode());
    }
    void limitsAreExplicit() {
        QImage tooWide(4097, 1, QImage::Format_ARGB32);
        QVERIFY(!ReliefAsset::fromImage(tooWide));
        QImage native(4096, 2, QImage::Format_RGBA64);
        native.fill(Qt::black);
        auto asset = ReliefAsset::fromImage(native);
        QVERIFY(asset);
        QCOMPARE(asset->image.size(), native.size());
        QVERIFY(asset->image.depth() > 32);
    }
    void referenceDither_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("Floyd-Steinberg")
            << QString("dither")
            << QByteArray("56f87a569f7ddfa8b1cd30daf0830038cfdd764b94354ffa8731418f0618dc34");
        QTest::newRow("Atkinson")
            << QString("atkinson")
            << QByteArray("859f1a51c431d9f68d4c8ef4638b6b4c4ab2782af0a185b24561963f649ab991");
    }
    void referenceDither() {
        QFETCH(QString, mode);
        QFETCH(QByteArray, expected);
        const int n = 512;
        QImage input(n, n, QImage::Format_ARGB32);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                int v = (x * 191 / (n - 1) + y * 63 / (n - 1) + ((x / 7 + y / 11) % 2) * 19) % 256;
                input.setPixel(x, y, qRgb(v, v, v));
            }
        QString error;
        auto out = ImageProcessing::apply(input, {{"imageMode", mode}}, error);
        QVERIFY(error.isEmpty());
        auto hash =
            QCryptographicHash::hash(
                QByteArrayView(reinterpret_cast<const char *>(out.constBits()), out.sizeInBytes()),
                QCryptographicHash::Sha256)
                .toHex();
        QCOMPARE(hash, expected);
    }
};
QTEST_MAIN(RasterTest)
#include "raster_test.moc"
