#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class MachinePlacementTest : public QObject {
    Q_OBJECT
  private slots:
    void idleSimulationSuppliesKnownHeadAndFreezesDuringExecution() {
        Document document; document.setBackgroundPlanning(false); document.newProject();
        document.addShape("rect"); document.setLayer(1);
        document.setProperty("x",10.); document.setProperty("y",20.);
        document.setProperty("width",40.); document.setProperty("height",20.);
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        QVERIFY(document.hasPreviewPosition());
        QCOMPARE(QPointF(machine.x(),machine.y()),QPointF(100,75));
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        auto placed = document.placementSummary()["graphicsBounds"].toMap();
        QCOMPARE(placed["x"].toDouble(),80.); QCOMPARE(placed["y"].toDouble(),65.);
        QVERIFY(document.placementSummary()["approachKnown"].toBool());
        QCOMPARE(document.job().moves[0].from,QPointF(100,75));
        machine.jog(10,0);
        placed = document.placementSummary()["graphicsBounds"].toMap();
        QCOMPARE(placed["x"].toDouble(),90.); QCOMPARE(placed["y"].toDouble(),65.);
        QVERIFY(document.setStartMode(Document::UserOrigin)); QVERIFY(document.captureUserOrigin());
        machine.jog(-10,0);
        placed = document.placementSummary()["graphicsBounds"].toMap();
        QCOMPARE(placed["x"].toDouble(),90.); // Saved origin remains110/75, not current100/75.
        QCOMPARE(document.job().moves[0].from,QPointF(100,75));
        const auto frozen = document.job().motionFingerprint();
        machine.start(); QVERIFY(machine.busy());
        QTest::qWait(200);
        QCOMPARE(document.job().motionFingerprint(),frozen);
        machine.stop(); QVERIFY(machine.ready());
        QCOMPARE(document.job().moves[0].from,QPointF(machine.x(),machine.y()));
        QVERIFY(document.placementSummary()["originConfirmed"].toBool());
    }
    void identicalModelDeviceChangesInvalidateUserOriginButDoNotCopyGrants() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false); document.newProject(); document.addShape("rect");
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        DeviceManager devices(&document,&machine,storage.filePath("devices.json"));
        auto a = devices.addCurrent("A","/dev/never-open-A");
        auto b = devices.addCurrent("B","/dev/never-open-B");
        QVERIFY(devices.activate(a)); QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.captureUserOrigin()); QVERIFY(document.placementSummary()["originConfirmed"].toBool());
        const auto origin = document.placementSummary()["userOrigin"];
        QVERIFY(devices.activate(b));
        QVERIFY(document.hasPreviewPosition());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(!document.job().valid());
        QCOMPARE(document.placementSummary()["userOrigin"],origin);
        QVERIFY(document.confirmUserOrigin()); QVERIFY(document.job().valid());
        QSignalSpy changed(&document,&Document::changed);
        machine.refreshPreviewPosition();
        QCOMPARE(changed.count(),0); // Same frame/point/revision neither dirties nor replans.
        QVERIFY(machine.simulation()); QVERIFY(machine.ports().isEmpty());
    }
    void portableIntentReopenNeverConfirmsOriginAndProviderRemainsIdempotent() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false); document.newProject(); document.addShape("rect");
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.captureUserOrigin());
        const auto url = QUrl::fromLocalFile(storage.filePath("intent.blazer"));
        QVERIFY(document.save(url)); QVERIFY(!document.dirty());
        QVERIFY(document.open(url));
        QVERIFY(document.hasPreviewPosition());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(!document.dirty()); QVERIFY(!document.job().valid());
        machine.refreshPreviewPosition();
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(document.confirmUserOrigin()); QVERIFY(document.job().valid());
    }
};
QTEST_MAIN(MachinePlacementTest)
#include "machineplacement_test.moc"
