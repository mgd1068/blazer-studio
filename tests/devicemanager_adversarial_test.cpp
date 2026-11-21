#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
class DeviceManagerAdversarialTest : public QObject {
    Q_OBJECT
  private slots:
    void externalMutationInvalidatesConnectionBinding() {
        QTemporaryDir dir;
        Document doc;
        Machine machine(&doc, nullptr, Machine::HardwareAccess::Disabled);
        const QString path = dir.filePath("devices.json");
        DeviceManager manager(&doc, &machine, path);
        const auto id = manager.addCurrent("first", "/dev/never-open-this");
        QVERIFY(!id.isEmpty());
        QVERIFY(manager.activate(id));
        QVERIFY(!manager.bindingKey().isEmpty());
        QFile altered(path);
        QVERIFY(altered.open(QIODevice::WriteOnly));
        altered.write("external change");
        altered.close();
        QVERIFY(!manager.update(id, "renamed", "/dev/never-open-this"));
        QVERIFY2(manager.bindingKey().isEmpty(),
                 "stale identity remains connectable after detected external mutation");
        QVERIFY(manager.active().isEmpty());
        QVERIFY(!manager.connectSelected());
        QVERIFY(!manager.message().contains("Offline-Lauf"));
    }
    void activationCannotReenterOldSelection() {
        QTemporaryDir dir;
        Document doc;
        Machine machine(&doc, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&doc, &machine, dir.filePath("devices.json"));
        const auto a = manager.addCurrent("a", "/dev/never-open-a");
        QVERIFY(manager.activate(a));
        auto descriptor = doc.deviceProfile().toJson();
        descriptor["name"] = "Other display name";
        QString error;
        auto renamed = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(renamed);
        QVERIFY(doc.setDeviceProfile(*renamed));
        const auto b = manager.addCurrent("b", "/dev/never-open-b");
        QVERIFY(!b.isEmpty());
        QVERIFY(manager.activate(a));
        bool callback = false, nestedActivate = false, nestedRemove = false, delegated = false;
        connect(&doc, &Document::deviceProfileChanged, &manager, [&] {
            if (callback)
                return;
            callback = true;
            manager.connectSelected();
            delegated = manager.message().contains("Offline-Lauf");
            nestedActivate = manager.activate(a);
            nestedRemove = manager.remove(a);
        });
        QVERIFY(manager.activate(b));
        QVERIFY(callback);
        QVERIFY2(!delegated,
                 "old selected identity delegated hardware connection during activation");
        QVERIFY(!nestedActivate);
        QVERIFY(!nestedRemove);
        QCOMPARE(manager.activeId(), b);
    }
};
QTEST_MAIN(DeviceManagerAdversarialTest)
#include "devicemanager_adversarial_test.moc"
