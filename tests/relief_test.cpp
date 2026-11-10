#include "document.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class ReliefTest : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    QUrl image(const QImage &image) {
        auto path = storage.filePath("map.png");
        return image.save(path) ? QUrl::fromLocalFile(path) : QUrl();
    }
    QImage ramp() {
        QImage image(3, 1, QImage::Format_ARGB32);
        image.setPixelColor(0, 0, Qt::white);
        image.setPixelColor(1, 0, QColor(128, 128, 128));
        image.setPixelColor(2, 0, Qt::black);
        return image;
    }
  private slots:
    void thresholdsAndTransparency() {
        ReliefSettings p;
        p.passes = 4;
        QCOMPARE(p.count(qRgb(255, 255, 255)), 0);
        QCOMPARE(p.count(qRgb(128, 128, 128)), 2);
        QCOMPARE(p.count(qRgb(0, 0, 0)), 4);
        p.inverted = true;
        QCOMPARE(p.count(qRgb(255, 255, 255)), 4);
        QCOMPARE(p.count(qRgb(0, 0, 0)), 0);
        QCOMPARE(p.count(qRgba(0, 0, 0, 0)), 0);
        QCOMPARE(p.count(qRgba(255, 255, 255, 0)), 0);
        p.inverted = false;
        p.gamma = 2;
        QCOMPARE(p.count(qRgb(128, 128, 128)), 1);
        auto asset = ReliefAsset::fromImage(ramp());
        QVERIFY(asset);
        auto raster = ReliefRaster::build(*asset, p, QSizeF(3, 1));
        QVERIFY(raster.error.isEmpty());
        QCOMPARE(raster.counts.size(), 300);
        auto map = p.toMap();
        ReliefSettings decoded;
        QVERIFY(ReliefSettings::fromMap(map, decoded));
        QCOMPARE(decoded, p);
        map["passes"] = 3.5;
        QVERIFY(!ReliefSettings::fromMap(map, decoded));
        map = p.toMap();
        map["gamma"] = "1";
        QVERIFY(!ReliefSettings::fromMap(map, decoded));
    }
    void slicesHaveCorrectVisits() {
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(ramp())));
        d.setProperty("width", 3.);
        d.setProperty("height", 1.);
        d.setReliefSetting("interval", 1.);
        d.setReliefSetting("passes", 4);
        const auto job = d.job();
        QVERIFY2(job.valid(), qPrintable(job.errors.join(";")));
        QCOMPARE(job.workLength, 6.); // one gray pixel twice, one black pixel four times
        int previous = 0;
        double perSlice[5]{};
        for (const auto &move : job.moves) {
            QVERIFY(move.slice >= previous);
            previous = move.slice;
            if (!move.rapid) {
                QVERIFY(move.from.x() >= 11 && move.to.x() >= 11); // no white pixel
                QCOMPARE(move.to.y(), 10.5);
                perSlice[move.slice] += QLineF(move.from, move.to).length();
            }
        }
        QCOMPARE(perSlice[1], 2.);
        QCOMPARE(perSlice[2], 2.);
        QCOMPARE(perSlice[3], 1.);
        QCOMPARE(perSlice[4], 1.);
        QVERIFY(job.gcode().contains("(Relief slice 4)"));
        d.setReliefSetting("inverted", true);
        double length = d.job().workLength;
        QCOMPARE(length, 7.); // midpoint rounds to 3 inverted visits
    }
    void orientationAndCutOrdering() {
        QImage map(1, 2, QImage::Format_ARGB32);
        map.setPixelColor(0, 0, Qt::black); // top must remain top in world coordinates
        map.setPixelColor(0, 1, Qt::white);
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(map)));
        d.setProperty("width", 1.);
        d.setProperty("height", 2.);
        d.setReliefSetting("interval", 1.);
        d.setReliefSetting("passes", 1);
        d.setLayer(1);
        QCOMPARE(d.items()[0].layer, 0);
        d.addShape("rect");
        QVERIFY(d.job().valid());
        bool sawCut = false, sawRelief = false;
        for (const auto &m : d.job().moves) {
            if (m.layer == 1)
                sawCut = true;
            if (m.slice > 0 && !m.rapid) {
                QVERIFY(!sawCut);
                sawRelief = true;
                QCOMPARE(m.to.y(), 11.5);
            }
        }
        QVERIFY(sawRelief && sawCut);
    }
    void persistenceUndoAndCalibration() {
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(ramp())));
        d.setReliefSetting("passes", 8);
        d.setReliefSetting("depthPerPass", .025);
        d.setReliefSetting("gamma", 2.);
        auto url = QUrl::fromLocalFile(storage.filePath("saved.blazer"));
        QVERIFY(d.save(url));
        Document restored;
        QVERIFY(restored.open(url));
        QCOMPARE(restored.items()[0].relief, d.items()[0].relief);
        QCOMPARE(restored.items()[0].reliefAsset->image, d.items()[0].reliefAsset->image);
        QCOMPARE(restored.job().gcode(), d.job().gcode());
        d.setObjectSetting("power", 30.);
        QCOMPARE(d.items()[0].relief.depthPerPass, 0.);
        d.undo();
        QCOMPARE(d.items()[0].relief.depthPerPass, .025);
        d.setReliefSetting("interval", .2);
        QCOMPARE(d.items()[0].relief.depthPerPass, 0.);
        d.undo();
        d.duplicateSelected();
        QCOMPARE(d.count(), 2);
        QVERIFY(d.items()[0].reliefAsset == d.items()[1].reliefAsset);
        d.setReliefSetting("passes", 3);
        QCOMPARE(d.items()[0].relief.passes, 8);
        QCOMPARE(d.items()[1].relief.passes, 3);
        d.undo();
        QCOMPARE(d.items()[1].relief.passes, 8);
    }
    void rejectsCorruptAndOvercomplexJobs() {
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(ramp())));
        auto url = QUrl::fromLocalFile(storage.filePath("bad.blazer"));
        QVERIFY(d.save(url));
        QFile f(url.toLocalFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        auto objects = json["objects"].toArray();
        auto first = objects[0].toObject();
        first["depthMap"] = "invalid!";
        objects[0] = first;
        json["objects"] = objects;
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(json).toJson());
        f.close();
        const auto old = d.items()[0].reliefAsset;
        QVERIFY(!d.open(url));
        QVERIFY(d.items()[0].reliefAsset == old);
        d.setProperty("width", 190.);
        d.setProperty("height", 140.);
        d.setReliefSetting("interval", .02);
        d.setReliefSetting("passes", 256);
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().moves.isEmpty());
        d.setProperty("x", 20.);
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().moves.isEmpty());
        QImage huge(4097, 1, QImage::Format_ARGB32);
        QVERIFY(!ReliefAsset::fromImage(huge));
    }
    void decodedImageBudget() {
        QImage map(512, 512, QImage::Format_ARGB32);
        map.fill(Qt::black);
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(map)));
        for (int i = 1; i < 128; ++i)
            d.duplicateSelected();
        QCOMPARE(d.count(), 128);
        d.duplicateSelected();
        QCOMPARE(d.count(), 128);
    }
    void whiteMapDoesNotEmit() {
        QImage map(2, 2, QImage::Format_ARGB32);
        map.fill(Qt::white);
        Document d;
        d.newProject();
        QVERIFY(d.importRelief(image(map)));
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().gcode().isEmpty());
        d.setReliefSetting("inverted", true);
        QVERIFY(d.job().valid());
    }
};
QTEST_MAIN(ReliefTest)
#include "relief_test.moc"
