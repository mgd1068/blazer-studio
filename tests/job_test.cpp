#include "document.h"
#include "job.h"
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
class JobTest : public QObject {
    Q_OBJECT
  private slots:
    void rectangleCut() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 10.);
        d.setProperty("y", 20.);
        d.setProperty("width", 30.);
        d.setProperty("height", 10.);
        auto j = d.job();
        QVERIFY2(j.valid(), qPrintable(j.errors.join(";")));
        QCOMPARE(j.workLength, 80.);
        auto lines = j.gcode();
        QCOMPARE(lines.first(), "M5");
        QVERIFY(lines.contains("G21"));
        QVERIFY(lines.contains("M4 S800"));
        QCOMPARE(lines.last(), "G0");
        QVERIFY(lines.contains("M5 S0"));
        for (const auto &line : lines)
            QVERIFY(line.size() < 79);
    }
    void filledEllipseAndText() {
        Document d;
        d.newProject();
        d.addShape("ellipse");
        d.setLayer(0);
        auto j = d.job();
        QVERIFY2(j.valid(), qPrintable(j.errors.join(";")));
        for (auto m : j.moves)
            if (!m.rapid)
                QCOMPARE(m.from.y(), m.to.y());
        d.newProject();
        d.addShape("text");
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
    }
    void svgAndHole() {
        QTemporaryDir dir;
        QFile f(dir.filePath("test.svg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='20mm' viewBox='0 0 "
                "20 20'><path fill-rule='evenodd' d='M0 0H20V20H0Z M5 5H15V15H5Z'/></svg>");
        f.close();
        Document d;
        d.newProject();
        QVERIFY(d.importSvg(QUrl::fromLocalFile(f.fileName())));
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        auto b = d.items().first().bounds;
        for (auto m : d.job().moves)
            if (!m.rapid && m.to.y() > b.y() + 5 && m.to.y() < b.y() + 15) {
                QVERIFY(!(m.from.x() < b.x() + 10 && m.to.x() > b.x() + 10));
                QVERIFY(!(m.to.x() < b.x() + 10 && m.from.x() > b.x() + 10));
            }
    }
    void svgWhiteKnockout() {
        QTemporaryDir dir;
        QFile f(dir.filePath("white.svg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='20mm' viewBox='0 0 "
                "20 20'><rect width='20' height='20' fill='black'/><rect x='5' y='5' width='10' "
                "height='10' fill='white'/></svg>");
        f.close();
        Document d;
        d.newProject();
        QVERIFY(d.importSvg(QUrl::fromLocalFile(f.fileName())));
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QVERIFY(std::abs(d.job().workLength - 3000.) < .01);
    }
    void rejectsOutsideAndSkipsHidden() {
        Document d;
        d.setProperty("x", 400.);
        QVERIFY(!d.job().valid());
        QVERIFY(d.job().gcode().isEmpty());
        d.toggleVisible(0);
        QVERIFY(d.job().valid());
    }
    void materialMatrixPersistence() {
        Document d;
        QVERIFY(d.createMaterialTest(1000, 3000, 10, 50, 4, 5));
        QCOMPARE(d.count(), 16);
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
        QSet<double> powers, speeds;
        for (auto m : d.job().moves)
            if (!m.rapid) {
                powers.insert(m.power);
                speeds.insert(m.speed);
            }
        QCOMPARE(powers.size(), 4);
        QCOMPARE(speeds.size(), 4);
        QVERIFY(d.selection()["customSettings"].toBool());
        QTemporaryDir dir;
        auto url = QUrl::fromLocalFile(dir.filePath("matrix.blazer"));
        QVERIFY(d.save(url));
        Document loaded;
        QVERIFY(loaded.open(url));
        QCOMPARE(loaded.job().gcode(), d.job().gcode());
        loaded.setObjectSetting("power", 25);
        QCOMPARE(loaded.items()[0].settings["power"].toDouble(), 25.);
        loaded.undo();
        QCOMPARE(loaded.job().gcode(), d.job().gcode());
        QVERIFY(!d.createMaterialTest(0, 1000, 10, 50, 4, 5));
        QCOMPARE(d.count(), 16);
    }
    void exportAndRaster() {
        QTemporaryDir dir;
        Document d;
        auto file = QUrl::fromLocalFile(dir.filePath("test.gcode"));
        QVERIFY(d.exportGcode(file));
        QFile f(file.toLocalFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().endsWith("M5 S0\nG0\n"));
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QVERIFY(image.save(dir.filePath("test.png")));
        d.newProject();
        QVERIFY(d.importImage(QUrl::fromLocalFile(dir.filePath("test.png"))));
        QVERIFY2(d.job().valid(), qPrintable(d.job().errors.join(";")));
    }
};
QTEST_MAIN(JobTest)
#include "job_test.moc"
