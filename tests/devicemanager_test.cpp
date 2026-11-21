#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "support/runtimepeer.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using RuntimeTestSupport::RuntimePeer;
namespace {
QString peerScript() {
    return QFileInfo(QString::fromUtf8(__FILE__))
        .absoluteDir()
        .filePath("support/runtime_pty_peer.py");
}
bool configurePeer(RuntimePeer &peer, const QJsonObject &values) {
    const auto reply = peer.control({{"action", "set"}, {"values", values}});
    return reply.ok && reply.object["accepted"].toBool();
}
} // namespace

class DeviceManagerTest : public QObject {
    Q_OBJECT
    static DeviceProfile named(const DeviceProfile &profile, const QString &name, int width = 200) {
        auto json = profile.toJson(); json["name"] = name;
        auto area = json["workspace"].toObject(); area["width"] = width; json["workspace"] = area;
        QString error;
        auto result = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        if (!result) qFatal("invalid fixture");
        return *result;
    }
  private slots:
    void asynchronousOpenAdmissionOwnsImmediateFenceAndCorrelatedResult() {
        RuntimePeer peer;
        QVERIFY(peer.start(peerScript()).ok);
        QVERIFY(configurePeer(peer, {{"resetEnabled", false}}));
        QTemporaryDir storage;
        Document document;
        document.newProject();
        Machine machine(&document);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        const auto a = manager.addCurrent("Synthetic A", peer.port());
        const auto b = manager.addCurrent("Synthetic B", peer.port());
        QVERIFY(manager.activate(a));
        QSignalSpy completed(&machine, &Machine::connectionRequestFinished);
        QVERIFY(manager.connectSelected()); // Local admission; no synchronous port-open claim.
        QVERIFY(manager.connectionRequestPending());
        QVERIFY(machine.occupied());
        QVERIFY(machine.requestPending());
        QVERIFY(!machine.connectionVerified()); // Admission itself never proves handshake/verification.
        QVERIFY(!manager.connectSelected());
        QVERIFY(!manager.activate(b));
        QVERIFY(!manager.remove(a));
        QVERIFY(!manager.update(a, "Changed", peer.port()));
        QVERIFY(!document.setDeviceProfile(named(document.deviceProfile(), "Other profile", 300)));
        QTRY_VERIFY_WITH_TIMEOUT(machine.connected(), 3000);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.first()[1].toBool());
        QVERIFY(!manager.connectionRequestPending());
        QVERIFY(!machine.connectionVerified());
        QVERIFY(!manager.changeBlockReason().isEmpty());
        QVERIFY(peer.control({{"action", "reply"}, {"data", "Grbl 1.3a ['$' for help]\r\n"}})
                    .object["accepted"]
                    .toBool());
        QTRY_VERIFY_WITH_TIMEOUT(machine.connectionVerified(), 5000);
        QVERIFY(machine.statusFresh());
        QVERIFY(!machine.positionReferenced());
        machine.useSimulator();
        if (machine.connectionPhase() == "closing")
            QVERIFY(!machine.simulation());
        QTRY_VERIFY_WITH_TIMEOUT(machine.simulation(), 3000);
        QTRY_VERIFY(!machine.occupied());
        QVERIFY(manager.activate(b));
        const auto snapshot = peer.snapshot();
        QVERIFY2(snapshot.ok, qPrintable(snapshot.error));
        const auto wire = snapshot.object;
        QVERIFY(wire.contains("counts"));
    }
    void cancellingAdmittedOpenCannotLeaveManagerPendingOrReconnectAutomatically() {
        RuntimePeer peer;
        QVERIFY(peer.start(peerScript()).ok);
        QVERIFY(configurePeer(peer, {{"resetEnabled", false}}));
        QTemporaryDir storage;
        Document document;
        document.newProject();
        Machine machine(&document);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("Synthetic cancellable", peer.port())));
        QVERIFY(manager.connectSelected());
        QVERIFY(manager.connectionRequestPending());
        machine.useSimulator(); // Before processing any GUI-side open result.
        QTRY_VERIFY_WITH_TIMEOUT(machine.simulation(), 3000);
        QTRY_VERIFY(!machine.occupied());
        QTRY_VERIFY(!manager.connectionRequestPending());
        QTest::qWait(30);
        QVERIFY(machine.simulation()); // Late Open result cannot switch back.
        QVERIFY(manager.connectSelected());
        QTRY_VERIFY_WITH_TIMEOUT(machine.connected(), 3000);
        QVERIFY(!machine.connectionVerified());
        machine.useSimulator();
        QTRY_VERIFY(machine.simulation());
        QTRY_VERIFY(!manager.connectionRequestPending());
    }
    void admittedMissingPortFailsAsynchronouslyAndOldResultCannotCompleteNextAttempt() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        Machine machine(&document);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        const auto endpoint = storage.filePath("missing-port");
        QVERIFY(!QFileInfo::exists(endpoint));
        const auto a = manager.addCurrent("Missing synthetic endpoint", endpoint);
        QVERIFY(manager.activate(a));
        QSignalSpy completed(&machine, &Machine::connectionRequestFinished);
        QVERIFY(manager.connectSelected());
        QVERIFY(manager.connectionRequestPending());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 3000);
        QVERIFY(!completed.first()[1].toBool());
        QVERIFY(!manager.connectionRequestPending());
        QVERIFY(!machine.connected());
        QVERIFY(!manager.message().isEmpty());
        QVERIFY(!manager.message().contains("Anfrage angenommen"));
        const auto oldId = completed.first()[0].toULongLong();
        QTRY_VERIFY(!machine.occupied());
        QVERIFY(manager.connectSelected());
        QVERIFY(manager.connectionRequestPending());
        const auto pendingMessage = manager.message();
        machine.connectionRequestFinished(oldId, true,
                                          "Stale result must not replace current request");
        QVERIFY(manager.connectionRequestPending());
        QCOMPARE(manager.message(), pendingMessage);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 3,
                                  3000); // One injected old result, then actual new result.
        QVERIFY(!completed.last()[1].toBool());
        QVERIFY(!manager.connectionRequestPending());
        machine.useSimulator();
        QTRY_VERIFY(machine.simulation());
    }
    void maskedParameterHashTracksActualObjectParameters() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        document.addShape("rect");
        document.setObjectSetting("power", 23.);
        document.setObjectSetting("speed", 731.);
        document.addShape("rect");
        document.setObjectSetting("power", 61.);
        document.setObjectSetting("speed", 1423.);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("Offline fixture", "")));
        const auto all = manager.parameterHash();
        QVERIFY(all.size() == 64);
        document.select(0);
        QCOMPARE(manager.parameterHash(),
                 all); // Editor selection is irrelevant without output masking.
        document.select(1);
        QCOMPARE(manager.parameterHash(), all);
        document.setCutSelectedGraphics(true);
        const auto second = manager.parameterHash();
        QSignalSpy notified(&manager, &DeviceManager::changed);
        document.select(0);
        QVERIFY(notified.size() > 0);
        const auto first = manager.parameterHash();
        QVERIFY2(first != second,
                 "Different selected power/feed must not reuse the previous material context.");
        QVERIFY2(second != all,
                 "Enabling selected output must change the material parameter contract.");
        QVERIFY(document.job().valid());
        for (const auto &move : document.job().moves)
            if (!move.rapid && move.power > 0) {
                QCOMPARE(move.power, 23.);
                QCOMPARE(move.speed, 731.);
            }
        document.setProperty("x", 45.);
        document.setProperty("width", 17.);
        QCOMPARE(manager.parameterHash(), first); // Layout alone is not a material recipe.
        document.select(1);
        document.setObjectSetting("power", 62.);
        document.select(0);
        QCOMPARE(manager.parameterHash(),
                 first); // An unselected object's override is not the active recipe.
        document.select(-1);
        const auto empty = manager.parameterHash();
        QVERIFY(!document.job().valid());
        QVERIFY(empty != first);
        QVERIFY(empty != second);
        document.setCutSelectedGraphics(false);
        QVERIFY(manager.parameterHash() != empty);
    }
    void parameterHashExcludesIneligibleObjectsButRetainsExtraOperations() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        document.addShape("rect");
        document.setObjectSetting("power", 23.);
        document.addShape("rect");
        document.setObjectSetting("power", 61.);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("Offline fixture", "")));
        document.setCutSelectedGraphics(true);
        // Selected helpers and hidden graphics never turn their overrides into output parameters.
        QVERIFY(document.setToolLayer(1));
        auto before = manager.parameterHash();
        document.setObjectSetting("power", 62.);
        QCOMPARE(manager.parameterHash(), before);
        QVERIFY(document.setToolLayer(2));
        before = manager.parameterHash();
        document.setObjectSetting("power", 63.);
        QCOMPARE(manager.parameterHash(), before);
        QVERIFY(document.setToolLayer(0));
        document.toggleVisible(1);
        before = manager.parameterHash();
        document.setObjectSetting("power", 64.);
        QCOMPARE(manager.parameterHash(), before);
        document.toggleVisible(1);
        // A disabled primary operation excludes the shape until an enabled extra operation exists.
        document.setLayerSetting(1, "enabled", false);
        before = manager.parameterHash();
        document.setObjectSetting("power", 65.);
        QCOMPARE(manager.parameterHash(), before);
        document.assignOperations({1, 0});
        const auto extra = manager.parameterHash();
        QVERIFY2(extra != before,
                 "Enabled extra operation is genuine output even when primary is disabled.");
        document.setLayerSetting(0, "power", 34.);
        QVERIFY(manager.parameterHash() != extra);
        QVERIFY(document.job().valid());
        document.assignOperations({1});
        QVERIFY(!document.job().valid());
        document.setLayerSetting(1, "enabled", true);
        QVERIFY(document.job().valid());
    }
    void identicalModelsHaveDistinctBindingsAndNeverAutoConnect() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false); document.newProject();
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        QCOMPARE(manager.instances().size(), 0); QVERIFY(manager.activeId().isEmpty());
        auto a = manager.addCurrent("Werkbank links", "/dev/never-open-A");
        auto b = manager.addCurrent("Werkbank rechts", "/dev/never-open-B");
        QVERIFY(!a.isEmpty()); QVERIFY(!b.isEmpty()); QVERIFY(a != b);
        QVERIFY(manager.activate(a)); const auto aKey = manager.bindingKey();
        QVERIFY(manager.activate(b)); const auto bKey = manager.bindingKey();
        QVERIFY(!aKey.isEmpty()); QVERIFY(aKey != bKey);
        QCOMPARE(manager.active()["endpoint"].toString(), QString("/dev/never-open-B"));
        QVERIFY(machine.simulation()); QVERIFY(machine.ports().isEmpty());
        QVERIFY(manager.activate(a)); QCOMPARE(manager.bindingKey(), aKey);
        QVERIFY(!manager.connectSelected()); // Disabled access rejects before QSerialPort.
        QVERIFY(machine.simulation());
        QVERIFY(manager.update(a, "Anderer Name", "/dev/never-open-A"));
        QCOMPARE(manager.bindingKey(), aKey);
        QVERIFY(manager.update(a, "Anderer Anschluss", "/dev/never-open-C"));
        QVERIFY(manager.bindingKey().isEmpty()); QVERIFY(manager.activeId().isEmpty());
        QVERIFY(manager.activate(a)); QVERIFY(manager.bindingKey() != aKey);
        auto copy = manager.duplicate(a, "Drittes Gerät"); QVERIFY(!copy.isEmpty());
        QVERIFY(manager.details(copy)["endpoint"].toString().isEmpty());
    }
    void activeFocusAndRunningSimulationBlockAllIdentityMutations() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false); document.newProject(); document.addShape("rect");
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        auto id = manager.addCurrent("Laser", ""); QVERIFY(manager.activate(id));
        const auto key = manager.bindingKey();
        machine.focus(true); QVERIFY(machine.firing());
        QVERIFY(!manager.update(id, "Changed", "changed"));
        QVERIFY(manager.addCurrent("Other", "").isEmpty());
        QVERIFY(manager.duplicate(id, "Copy").isEmpty());
        QVERIFY(!manager.remove(id)); QVERIFY(!manager.activate(id));
        QVERIFY(!manager.connectSelected()); QCOMPARE(manager.bindingKey(), key);
        machine.focus(false); machine.start(); QVERIFY(machine.busy());
        QVERIFY(!manager.remove(id)); QVERIFY(!manager.activate(id));
        machine.stop(); QVERIFY(manager.remove(id)); QVERIFY(manager.active().isEmpty());
    }
    void externalLibraryChangeRevokesAnOtherwiseValidBinding() {
        QTemporaryDir storage;
        const auto path = storage.filePath("devices.json");
        Document document; document.setBackgroundPlanning(false);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, path);
        const auto id = manager.addCurrent("Laser", "/dev/never-open");
        QVERIFY(manager.activate(id)); QVERIFY(!manager.bindingKey().isEmpty());
        QFile disk(path); QVERIFY(disk.open(QIODevice::WriteOnly));
        QCOMPARE(disk.write("external edit"), qint64(13)); disk.close();
        QVERIFY(!manager.update(id, "Renamed", "/dev/never-open"));
        QVERIFY(manager.bindingKey().isEmpty()); QVERIFY(manager.active().isEmpty());
        QVERIFY(!manager.connectSelected()); QVERIFY(machine.simulation());
        QVERIFY(manager.message().contains("extern"));
        QVERIFY(disk.open(QIODevice::ReadOnly)); QCOMPARE(disk.readAll(), QByteArray("external edit"));
    }
    void activationCannotReenterConnectionOrLibraryMutation() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        auto a = manager.addCurrent("A", "/dev/never-open-A"); QVERIFY(manager.activate(a));
        QVERIFY(document.setDeviceProfile(named(document.deviceProfile(), "Named B")));
        auto b = manager.addCurrent("B", "/dev/never-open-B"); QVERIFY(manager.activate(a));
        bool callbackRan = false;
        connect(&document, &Document::deviceProfileChanged, &manager, [&] {
            callbackRan = true;
            QVERIFY(!manager.connectSelected());
            QVERIFY(!manager.activate(a));
            QVERIFY(!manager.remove(a));
        });
        QVERIFY(manager.activate(b)); QVERIFY(callbackRan);
        QCOMPARE(manager.activeId(), b); QCOMPARE(manager.instances().size(), 2);
        QCOMPARE(manager.active()["endpoint"].toString(), QString("/dev/never-open-B"));
        QVERIFY(machine.simulation());
    }
    void profileChangesInvalidateSelectionAndProfileImportIsAtomic() {
        QTemporaryDir storage;
        Document document; document.setBackgroundPlanning(false);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document, &machine, storage.filePath("devices.json"));
        auto id = manager.addCurrent("A", ""); QVERIFY(manager.activate(id));
        QVERIFY(document.setDeviceProfile(named(document.deviceProfile(), "Larger", 300)));
        QVERIFY(manager.bindingKey().isEmpty());
        const auto file = storage.filePath("profile.json");
        QFile data(file); QVERIFY(data.open(QIODevice::WriteOnly));
        auto bytes = QJsonDocument(document.deviceProfile().toJson()).toJson();
        QCOMPARE(data.write(bytes), qint64(bytes.size())); data.close();
        auto imported = manager.importProfile(QUrl::fromLocalFile(file), "Larger device", "");
        QVERIFY(!imported.isEmpty()); QVERIFY(manager.activate(imported));
        QCOMPARE(document.bedWidth(), 300.);
        const auto count = manager.instances().size();
        const auto active = manager.activeId();
        QVERIFY(manager.importProfile(QUrl("https://invalid.test/profile"), "Remote", "").isEmpty());
        QVERIFY(data.open(QIODevice::WriteOnly)); data.write(QByteArray(65537, 'x')); data.close();
        QVERIFY(manager.importProfile(QUrl::fromLocalFile(file), "Oversize", "").isEmpty());
        QCOMPARE(manager.instances().size(), count); QCOMPARE(manager.activeId(), active);
    }
};
QTEST_MAIN(DeviceManagerTest)
#include "devicemanager_test.moc"
