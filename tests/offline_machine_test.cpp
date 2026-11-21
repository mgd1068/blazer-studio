#include "document.h"
#include "machine.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

// Existing named private friend, in a separate test executable. It inspects the
// absence of a transport; it grants no hardware/synthetic motion capability.
class GrblTest : public QObject {
    Q_OBJECT
  private slots:
    void disabledFacadeNeverConstructsATransport() {
        Document document;
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        QVERIFY(!machine.session);
        QVERIFY(machine.simulation());
        QVERIFY(machine.ports().isEmpty());
        QVERIFY(!machine.connectHardware("offline-not-a-device"));
        QVERIFY(!machine.session);
        QVERIFY(machine.simulation());
    }
    void profileChangesKeepTheFacadeTransportFree() {
        Document document;
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        auto data=document.deviceProfile().toJson();
        auto area=data["workspace"].toObject();
        area["width"]=400; area["height"]=300; data["workspace"]=area;
        QString error;
        const auto profile=DeviceProfile::parse(QJsonDocument(data).toJson(),error);
        QVERIFY2(profile,qPrintable(error));
        QVERIFY(document.setDeviceProfile(*profile));
        QCOMPARE(document.workArea(),QSizeF(400,300));
        QVERIFY(!machine.session);
        QVERIFY(machine.simulation());
    }
    void ordinaryProjectExportDoesNotNeedAControllerEventLoop() {
        QTemporaryDir directory;
        Document document;
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        document.newProject();
        document.addShape("rect");
        QVERIFY(document.job().valid());
        const auto path=directory.filePath("offline.gcode");
        QVERIFY(document.exportGcode(QUrl::fromLocalFile(path)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes=file.readAll();
        QVERIFY(bytes.startsWith("M5\n"));
        QVERIFY(bytes.contains("G21\nG90\n"));
        QVERIFY(!machine.session);
        QVERIFY(machine.simulation());
    }
};
QTEST_MAIN(GrblTest)
#include "offline_machine_test.moc"
