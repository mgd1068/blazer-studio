#include "document.h"
#include "job.h"
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
class JobTest : public QObject {
    Q_OBJECT
  private slots:
    void alternativePowerScaleKeepsInlineChangesAndZeroOutput() {
        auto descriptor = DeviceProfile::m3Reference().toJson();
        auto driver = descriptor["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        descriptor["driver"] = driver;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        JobPlan plan(*profile);
        plan.moves = {{{0, 0}, {1, 0}, 3000, 0, 0, false},
                      {{1, 0}, {2, 0}, 3000, 2, 0, false},
                      {{2, 0}, {3, 0}, 3000, 50, 0, false},
                      {{3, 0}, {4, 0}, 3000, 100, 0, false},
                      {{4, 0}, {5, 0}, 3000, 0, 0, false}};
        const auto lines = plan.gcode();
        QVERIFY(lines.contains("M4 S0"));
        QVERIFY(lines.contains("G1 X2.000 Y0.000 S5"));
        QVERIFY(lines.contains("G1 X3.000 Y0.000 S128"));
        QVERIFY(lines.contains("G1 X4.000 Y0.000 S255"));
        QVERIFY(lines.contains("G1 X5.000 Y0.000 S0"));
        QVERIFY(lines.contains("M5 S0"));
        plan.cacheCommands();
        QCOMPARE(plan.gcode(), lines);
        QCOMPARE(plan.cachedCommands(), lines);
        const auto matrix = JobPlanner::materialTest(1000, 2000, 50, 100, 3, *profile);
        QVERIFY(matrix.valid());
        QCOMPARE(matrix.deviceProfile().outputFingerprint(), profile->outputFingerprint());
        QVERIFY(matrix.gcode().join("\n").contains("S255"));
    }
    void jobUsesFrozenDeviceAreaAndPowerScale() {
        Document document;
        document.newProject();
        document.addShape("rect");
        document.setProperty("x", 10.);
        document.setProperty("y", 20.);
        document.setProperty("width", 30.);
        document.setProperty("height", 10.);
        auto input = document.jobInput();
        const auto oldPlan = JobPlanner::build(input);
        QCOMPARE(oldPlan.deviceProfile().outputFingerprint(),
                 document.deviceProfile().outputFingerprint());
        auto descriptor = input.deviceProfile.toJson();
        auto workspace = descriptor["workspace"].toObject();
        workspace["width"] = 600;
        workspace["height"] = 400;
        descriptor["workspace"] = workspace;
        auto driver = descriptor["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        descriptor["driver"] = driver;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY2(profile, qPrintable(error));
        input.deviceProfile = *profile;
        input.items.first().bounds.moveTopLeft({400, 300});
        const auto plan = JobPlanner::build(input);
        QVERIFY2(plan.valid(), qPrintable(plan.errors.join(";")));
        QCOMPARE(plan.deviceProfile().workArea(), QSizeF(600, 400));
        QCOMPARE(plan.workLength, 80.);
        QVERIFY(plan.gcode().contains("M4 S204")); // 80% of 255, independent of wattage.
        QVERIFY2(plan.gcode().join("\n").contains("G1 X430.000 Y300.000"),
                 qPrintable(plan.gcode().join("\n")));
        QVERIFY(plan.gcode().join("\n").contains(" F600"));
        QVERIFY(plan.gcode().join("\n").contains("$30=255"));
        QVERIFY(oldPlan.gcode().contains("M4 S800"));
        QCOMPARE(oldPlan.deviceProfile().workArea(), QSizeF(200, 150));
        input.deviceProfile = document.deviceProfile();
        QVERIFY(!JobPlanner::build(input).valid());
        input.deviceProfile = *profile;
        input.items.first().bounds.moveLeft(580);
        QVERIFY(!JobPlanner::build(input).valid());
        auto cached = plan;
        cached.cacheCommands();
        input.deviceProfile = document.deviceProfile();
        QCOMPARE(cached.gcode(), plan.gcode());
        QCOMPARE(cached.summary()["outputFingerprint"].toString(),
                 QString::fromLatin1(profile->outputFingerprint()));
        auto laser = descriptor["laser"].toObject();
        laser["nominalPowerW"] = 10;
        descriptor["laser"] = laser;
        const auto otherHead = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(otherHead);
        input.deviceProfile = *otherHead;
        input.items.first().bounds.moveTopLeft({400, 300});
        const auto otherPlan = JobPlanner::build(input);
        QVERIFY(otherPlan.valid());
        QCOMPARE(otherPlan.gcode(), plan.gcode()); // Wattage is never a digital scale multiplier.
        QVERIFY(otherPlan.deviceProfile().outputFingerprint() !=
                plan.deviceProfile().outputFingerprint());
    }
    void profileLimitsApplyToOverscanMaterialTestsAndEncoding() {
        Document document;
        document.newProject();
        document.addShape("rect");
        auto input = document.jobInput();
        input.items.first().bounds = {400, 300, 30, 10};
        input.items.first().layer = 0;
        auto layer = input.layers[0].toMap();
        layer["overscan"] = 2.;
        input.layers[0] = layer;
        auto descriptor = input.deviceProfile.toJson();
        descriptor["workspace"] = QJsonObject{
            {"units", "mm"}, {"origin", "bottom-left"}, {"width", 430}, {"height", 400}};
        QString error;
        auto profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        input.deviceProfile = *profile;
        QVERIFY(!JobPlanner::build(input).valid()); // Two-mm overscan leaves this bed.
        auto workspace = descriptor["workspace"].toObject();
        workspace["width"] = 600;
        descriptor["workspace"] = workspace;
        profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        input.deviceProfile = *profile;
        const auto plan = JobPlanner::build(input);
        QVERIFY2(plan.valid(), qPrintable(plan.errors.join(";")));
        bool outsideM3 = false;
        for (const auto &move : plan.moves) {
            QVERIFY(move.to.x() <= 432.00001);
            QVERIFY(move.to.x() >= 397.99999);
            outsideM3 |= move.to.y() > 150;
        }
        QVERIFY(outsideM3);
        workspace["width"] = 20;
        workspace["height"] = 20;
        descriptor["workspace"] = workspace;
        profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        QVERIFY(!JobPlanner::materialTest(1000, 2000, 10, 20, 3, *profile).valid());
        descriptor["workspace"] = QJsonObject{
            {"units", "mm"}, {"origin", "bottom-left"}, {"width", 600}, {"height", 400}};
        descriptor["driver"] =
            QJsonObject{{"id", "test-galvo"}, {"transport", "usb"}, {"settings", QJsonObject{}}};
        profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        input.deviceProfile = *profile;
        const auto unsupported = JobPlanner::build(input);
        QVERIFY(!unsupported.valid());
        QVERIFY(unsupported.moves.isEmpty());
        QVERIFY(unsupported.gcode().isEmpty());
        JobPlan manual(*profile);
        manual.moves = plan.moves;
        QVERIFY(manual.gcode().isEmpty()); // No accidental GRBL encoding for another driver.
    }
    void rasterPowerChangesStayOnMotion() {
        JobPlan plan;
        plan.moves = {{{0, 0}, {7, 0}, 5000, 0, 0, false},
                      {{7, 0}, {9, 0}, 5000, 100, 0, false},
                      {{9, 0}, {16, 0}, 5000, 0, 0, false},
                      {{16, 0}, {16, .1}, 3000, 0, 0, true},
                      {{16, .1}, {9, .1}, 5000, 0, 0, false},
                      {{9, .1}, {7, .1}, 5000, 50, 0, false},
                      {{7, .1}, {0, .1}, 5000, 0, 0, false}};
        const auto lines = plan.gcode();
        QVERIFY(lines.contains("M4 S0"));
        QVERIFY(!lines.contains("M4 S1000"));
        QVERIFY(lines.contains("G1 X9.000 Y0.000 S1000"));
        QVERIFY(lines.contains("G1 X16.000 Y0.000 S0"));
        QVERIFY(lines.contains("G1 X7.000 Y0.100 S500"));
        QVERIFY(lines.contains("G1 X0.000 Y0.100 S0"));
        QVERIFY(!lines.contains("M4 S500"));
        QCOMPARE(lines.count("M4 S0"), 2);
        const int rapid = lines.indexOf("G0 X16.000 Y0.100");
        QVERIFY(rapid > 0);
        QCOMPARE(lines[rapid - 1], "M5");
        QCOMPARE(lines[rapid + 1], "M4 S0");
        QVERIFY(lines.contains("M5 S0"));
    }
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
