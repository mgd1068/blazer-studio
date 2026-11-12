#include "document.h"
#include "imageprocessing.h"
#include "recovery.h"
#include "registration.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
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
  private slots:
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
