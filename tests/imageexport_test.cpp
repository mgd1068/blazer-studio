#include "document.h"
#include "imageexport.h"
#include "imageprocessing.h"
#include <QFile>
#include <QImageReader>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ImageExportTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QUrl target(const QString &name) const { return QUrl::fromLocalFile(temp.filePath(name)); }
    static QByteArray bytes(const QUrl &url) {
        QFile file(url.toLocalFile());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    ImageExportInput input() {
        QImage image(3, 2, QImage::Format_ARGB32);
        const int values[]{0, 64, 128, 192, 255, 32};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) {
                int v = values[y * 3 + x];
                image.setPixelColor(x, y, QColor(v, v, v));
            }
        return {ReliefAsset::fromImage(image), {{"imageMode", "grayscale"}}, QSizeF(20, 10), true};
    }
  private slots:
    void allAdvertisedRasterFormatsImport() {
        QImage source(16, 8, QImage::Format_RGB32);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                source.setPixelColor(x, y, x < 8 ? Qt::black : Qt::white);
        for (const QString extension : {"png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp"}) {
            const auto file = target("import." + extension);
            QVERIFY2(source.save(file.toLocalFile()), qPrintable(extension));
            Document d;
            d.newProject();
            QVERIFY2(d.importImage(file), qPrintable(d.message()));
            QCOMPARE(d.items()[0].reliefAsset->image.size(), source.size());
            QVERIFY(qGray(d.items()[0].reliefAsset->image.pixel(1, 1)) < 10);
            QVERIFY(qGray(d.items()[0].reliefAsset->image.pixel(14, 1)) > 245);
        }
        const auto gif = target("own.gif");
        QFile file(gif.toLocalFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex(
            "47494638396102000100800000000000ffffff2c0000000002000100000202440a003b"));
        file.close();
        Document d;
        d.newProject();
        QVERIFY(d.importImage(gif));
        QCOMPARE(d.items()[0].reliefAsset->image.size(), QSize(2, 1));
        QCOMPARE(qGray(d.items()[0].reliefAsset->image.pixel(0, 0)), 0);
        QCOMPARE(qGray(d.items()[0].reliefAsset->image.pixel(1, 0)), 255);
    }
    void processedPngHasAppliedCropAndInvertAndPhysicalSize() {
        auto request = input();
        request.style = {{"imageMode", "grayscale"},
                         {"cropX", 1},
                         {"cropY", 0},
                         {"cropW", 2},
                         {"cropH", 2},
                         {"invert", true}};
        const auto url = target("corrected.png");
        const auto result = ImageExport::run(request, url);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.cancelled);
        QCOMPARE(result.pixels, QSize(2, 2));
        QImage saved(url.toLocalFile());
        QCOMPARE(saved.size(), QSize(2, 2));
        const int expected[]{191, 127, 0, 223};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                QCOMPARE(saved.pixelColor(x, y),
                         QColor(expected[y * 2 + x], expected[y * 2 + x], expected[y * 2 + x]));
        QCOMPARE(saved.dotsPerMeterX(), 100);
        QCOMPARE(saved.dotsPerMeterY(), 200);
        QCOMPARE(request.asset->image.size(), QSize(3, 2));
        QCOMPARE(qGray(request.asset->image.pixel(1, 0)), 64);
        request.processed = false;
        QVERIFY(ImageExport::run(request, target("source.png")).error.isEmpty());
        QImage source(target("source.png").toLocalFile());
        QCOMPARE(source.size(), QSize(3, 2));
        QCOMPARE(source.convertToFormat(QImage::Format_ARGB32),
                 request.asset->image.convertToFormat(QImage::Format_ARGB32));
    }
    void allModesUseTheJobPipeline() {
        auto request = input();
        QImage maskSource(7, 5, QImage::Format_ARGB32);
        maskSource.fill(QColor(128, 128, 128));
        request.asset = ReliefAsset::fromImage(maskSource);
        for (const QString mode : {"grayscale", "threshold", "dither", "atkinson"}) {
            request.style = {{"imageMode", mode}, {"brightness", .1}, {"contrast", 1.4},
                             {"gamma", .7},       {"sharpness", 1.2}, {"ellipseMask", true}};
            QString error;
            const auto expected =
                ImageProcessing::apply(request.asset->image, request.style, error);
            QVERIFY(error.isEmpty());
            const auto url = target(mode + ".png");
            QVERIFY(ImageExport::run(request, url).error.isEmpty());
            QCOMPARE(QImage(url.toLocalFile()).convertToFormat(QImage::Format_ARGB32), expected);
            QCOMPARE(QImage(url.toLocalFile()).pixelColor(0, 0).alpha(), 0);
            QCOMPARE(QImage(url.toLocalFile()).pixelColor(3, 2).alpha(), 255);
        }
    }
    void formatsFlattenOnlyWhenNeededAndRawPngKeeps16Bits() {
        QImage image(4, 3, QImage::Format_RGBA64);
        image.fill(Qt::transparent);
        image.setPixelColor(1, 1, QColor::fromRgba64(12345, 12345, 12345, 65535));
        image.setText("Description", "private source metadata");
        image.setDevicePixelRatio(2);
        auto asset = std::make_shared<ReliefAsset>();
        asset->image = image;
        ImageExportInput request{asset, {}, QSizeF(4, 3), false};
        for (const QString ext : {"png", "jpg", "jpeg", "bmp"}) {
            const auto url = target("alpha." + ext);
            const auto r = ImageExport::run(request, url);
            QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
            QImage saved(url.toLocalFile());
            QCOMPARE(saved.size(), QSize(4, 3));
            QVERIFY(!saved.textKeys().contains("Description"));
            if (ext == "png") {
                QCOMPARE(saved.pixelColor(0, 0).alpha(), 0);
                QVERIFY(saved.depth() > 32);
                QCOMPARE(saved.pixelColor(1, 1).rgba64().red(), quint16(12345));
            } else {
                QVERIFY(saved.pixelColor(0, 0).alpha() == 255);
                QVERIFY(saved.pixelColor(0, 0).red() >= 245);
            }
        }
        QCOMPARE(image.devicePixelRatio(), 2.);
        QCOMPARE(image.text("Description"), QString("private source metadata"));
    }
    void errorsAndCancellationPreserveExistingTargets() {
        const auto url = target("existing.png");
        QFile existing(url.toLocalFile());
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("previous");
        existing.close();
        auto request = input();
        auto cancelled = std::make_shared<std::atomic_bool>(true);
        QVERIFY(ImageExport::run(request, url, cancelled).cancelled);
        QCOMPARE(bytes(url), QByteArray("previous"));
        request.style["gamma"] = 0.;
        QVERIFY(!ImageExport::run(request, url).error.isEmpty());
        QCOMPARE(bytes(url), QByteArray("previous"));
        request = input();
        QLockFile lock(url.toLocalFile() + ".lock");
        QVERIFY(lock.tryLock());
        QVERIFY(!ImageExport::run(request, url).error.isEmpty());
        QCOMPARE(bytes(url), QByteArray("previous"));
        lock.unlock();
        QVERIFY(!ImageExport::run(request, target("missing/export.png")).error.isEmpty());
        QVERIFY(
            !ImageExport::run(request, QUrl("https://example.invalid/image.png")).error.isEmpty());
        QVERIFY(!ImageExport::run(request, target("unsupported.gif")).error.isEmpty());
        const auto link = target("linked.png");
        QVERIFY(QFile::link(url.toLocalFile(), link.toLocalFile()));
        QVERIFY(!ImageExport::run(request, link).error.isEmpty());
        QCOMPARE(bytes(url), QByteArray("previous"));
        request.asset.reset();
        QVERIFY(!ImageExport::run(request, url).error.isEmpty());
        QCOMPARE(bytes(url), QByteArray("previous"));
    }
    void backgroundRequestIsDetachedAndReportsCompletion() {
        ImageExport exporter;
        QSignalSpy done(&exporter, &ImageExport::finished);
        QString error;
        auto request = input();
        request.style["invert"] = true;
        const auto url = target("background.png");
        QVERIFY(exporter.start(request, url, error));
        QVERIFY(exporter.busy());
        QVERIFY(!exporter.start(input(), target("overlap.png"), error));
        request.style["invert"] = false;
        request.asset.reset();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(!exporter.busy());
        QCOMPARE(QImage(url.toLocalFile()).pixelColor(0, 0), QColor(Qt::white));
        QVERIFY(!QFile::exists(target("overlap.png").toLocalFile()));
        QVERIFY(done[0][0].toString().contains("exportiert"));
    }
    void documentSelectionStaleDialogAndHistoryRemainCorrect() {
        auto request = input();
        const auto source = target("input.png");
        QVERIFY(request.asset->image.save(source.toLocalFile()));
        Document d;
        d.newProject();
        QVERIFY(!d.exportImage(target("empty.png")));
        QVERIFY(d.importImage(source));
        QVERIFY(d.command("imageSettings", {{"imageMode", "grayscale"}, {"invert", true}}));
        const auto id = d.selection()["id"].toString();
        const auto revision = d.historyRevision();
        QVERIFY(d.setMetadata("geändert", "Notizen"));
        QVERIFY(!d.exportImage(target("stale.png"), true, id, revision));
        QVERIFY(!QFile::exists(target("stale.png").toLocalFile()));
        const auto before = d.projectBytes();
        const auto currentRevision = d.historyRevision();
        const auto dirty = d.dirty();
        QSignalSpy reset(&d, &QAbstractItemModel::modelReset);
        const auto out = target("document.png");
        QVERIFY(d.exportImage(out, true, id, currentRevision));
        QVERIFY(d.imageExportBusy());
        QCOMPARE(d.projectBytes(), before);
        QCOMPARE(d.historyRevision(), currentRevision);
        QCOMPARE(d.dirty(), dirty);
        QCOMPARE(reset.count(), 0);
        d.newProject();
        d.addShape("rect");
        const auto replacement = d.projectBytes();
        QTRY_VERIFY_WITH_TIMEOUT(!d.imageExportBusy(), 5000);
        QCOMPARE(d.projectBytes(), replacement);
        QCOMPARE(QImage(out.toLocalFile()).pixelColor(0, 0), QColor(Qt::white));
        QVERIFY(!d.exportImage(target("vector.png")));
    }
};
QTEST_MAIN(ImageExportTest)
#include "imageexport_test.moc"
