#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "support/runtimepeer.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using RuntimeTestSupport::RuntimePeer;
namespace {
QString key(const QString &binding) {
    return "devices/autoHomeOnConnect/v1/" + binding;
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(bytes), bytes.size());
}
QString peerScript() {
    return QFileInfo(QString::fromUtf8(__FILE__))
        .absoluteDir()
        .filePath("support/runtime_pty_peer.py");
}
int commands(RuntimePeer &peer) {
    return peer.snapshot().object.value("counts").toObject().value("command").toInt();
}
void noHome(RuntimePeer &peer) {
    const auto s = peer.snapshot();
    QVERIFY(s.ok);
    for (const auto &v : s.object.value("events").toArray()) {
        const auto e = v.toObject();
        if (e.value("kind") == "command")
            QVERIFY(e.value("value").toString() != "$H");
    }
}
} // namespace
class DeviceAutoHomePreferenceTest : public QObject {
    Q_OBJECT
    struct ConnectedCleanup {
        Machine &m;
        ~ConnectedCleanup() {
            m.useSimulator();
            QElapsedTimer t;
            t.start();
            while ((!m.simulation() || m.connectionPhase() != "closed") && t.elapsed() < 3000) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
        }
    };
  private slots:
    void init() {
        // Each test has a distinct local settings file under the isolated XDG root.
        QCoreApplication::setApplicationName("DeviceAutoHomePreference-" +
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    void defaultFalseNoSelectionAndTogglingNeverConnects() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QSignalSpy requests(&m, &Machine::connectionRequestFinished);
        QVERIFY(!manager.autoHomeOnConnect());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(!manager.autoHomeOnConnect());
        QVERIFY(!manager.message().isEmpty());
        const auto id = manager.addCurrent("Synthetic A", "not-an-opened-port");
        QVERIFY(!id.isEmpty());
        QVERIFY(manager.activate(id));
        QCOMPARE(manager.bindingKey().size(), 64);
        QVERIFY(!manager.autoHomeOnConnect());
        const auto position = QPointF(m.x(), m.y());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(manager.autoHomeOnConnect());
        QVERIFY(manager.message().contains("nächsten"));
        manager.setAutoHomeOnConnect(false);
        QVERIFY(!manager.autoHomeOnConnect());
        QVERIFY(m.simulation());
        QVERIFY(!m.requestPending());
        QVERIFY(!manager.connectionRequestPending());
        QCOMPARE(requests.count(), 0);
        QCOMPARE(QPointF(m.x(), m.y()), position);
    }
    void persistenceIsPerBindingAndEndpointRevisionOrInstanceDoesNotInherit() {
        QTemporaryDir t;
        const auto path = t.filePath("devices.json");
        QString a, b, originalBinding;
        {
            Document d;
            d.setBackgroundPlanning(false);
            d.newProject();
            Machine m(&d);
            DeviceManager manager(&d, &m, path);
            a = manager.addCurrent("A", "synthetic-a");
            b = manager.addCurrent("B", "synthetic-a");
            QVERIFY(manager.activate(a));
            originalBinding = manager.bindingKey();
            manager.setAutoHomeOnConnect(true);
            QVERIFY(manager.autoHomeOnConnect());
            QVERIFY(manager.activate(b));
            QVERIFY(!manager.autoHomeOnConnect());
            QVERIFY(manager.activate(a));
            QVERIFY(manager.autoHomeOnConnect());
            const auto duplicate = manager.duplicate(a, "A copy");
            QVERIFY(!duplicate.isEmpty());
            QVERIFY(manager.activate(duplicate));
            QVERIFY(!manager.autoHomeOnConnect());
            QVERIFY(manager.activate(a));
            QVERIFY(manager.update(a, "Renamed A", "synthetic-a"));
            QCOMPARE(manager.bindingKey(), originalBinding);
            QVERIFY(manager.autoHomeOnConnect());
        }
        {
            Document d;
            d.setBackgroundPlanning(false);
            d.newProject();
            Machine m(&d);
            DeviceManager manager(&d, &m, path);
            QVERIFY(manager.activate(a));
            QVERIFY(manager.autoHomeOnConnect());
            QVERIFY(manager.update(a, "Renamed A", "synthetic-new-endpoint"));
            QVERIFY(manager.bindingKey().isEmpty());
            QVERIFY(!manager.autoHomeOnConnect());
            QVERIFY(manager.activate(a));
            QVERIFY(manager.bindingKey() != originalBinding);
            QVERIFY(!manager.autoHomeOnConnect());
            QSettings settings;
            QCOMPARE(settings.value(key(originalBinding)).toBool(), true);
        }
    }
    void profileRevisionGetsANewDefaultFalsePreference() {
        QTemporaryDir t;
        const auto path = t.filePath("devices.json");
        QString id, oldBinding;
        DeviceProfile profile = DeviceProfile::m3Reference();
        {
            Document d;
            d.setBackgroundPlanning(false);
            d.newProject();
            Machine m(&d);
            DeviceManager manager(&d, &m, path);
            id = manager.addCurrent("A", "synthetic");
            QVERIFY(manager.activate(id));
            oldBinding = manager.bindingKey();
            profile = d.deviceProfile();
            manager.setAutoHomeOnConnect(true);
            QVERIFY(manager.autoHomeOnConnect());
        }
        auto json = profile.toJson();
        auto workspace = json["workspace"].toObject();
        workspace["width"] = 210.;
        json["workspace"] = workspace;
        QString error;
        const auto changed = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY(changed);
        {
            DeviceLibrary library(path);
            QVERIFY(library.update(id, "A", *changed, "synthetic"));
        }
        {
            Document d;
            d.setBackgroundPlanning(false);
            d.newProject();
            Machine m(&d);
            DeviceManager manager(&d, &m, path);
            QVERIFY(manager.activate(id));
            QVERIFY(manager.bindingKey() != oldBinding);
            QVERIFY(!manager.autoHomeOnConnect());
            QSettings settings;
            QVERIFY(settings.value(key(oldBinding)).toBool());
        }
    }
    void onlyBoolOrExactQtIniRoundtripTokensAreAccepted() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("A", "synthetic")));
        const auto setting = key(manager.bindingKey());
        for (const auto &value : QVariantList{true, QString("true")}) {
            QSettings s;
            s.setValue(setting, value);
            s.sync();
            QCOMPARE(s.status(), QSettings::NoError);
            QVERIFY(manager.autoHomeOnConnect());
        }
        for (const auto &value :
             QVariantList{false, QString("false"), QString("TRUE"), QString("yes"), QString("1"), 1,
                          QByteArray("true"), QString(" true ")}) {
            QSettings s;
            s.setValue(setting, value);
            s.sync();
            QCOMPARE(s.status(), QSettings::NoError);
            QVERIFY(!manager.autoHomeOnConnect());
        }
        QVERIFY(manager.activate(manager.activeId()));
        QVERIFY(manager.message().contains("ungültig"));
        manager.setAutoHomeOnConnect(false);
        QVERIFY(!manager.autoHomeOnConnect());
        QSettings s;
        QVERIFY(s.value(setting).metaType().id() == QMetaType::Bool);
        QCOMPARE(s.value(setting).toBool(), false);
    }
    void corruptOrUnwritableSettingsFailOffWithoutSideEffects() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("A", "synthetic")));
        manager.setAutoHomeOnConnect(true);
        QVERIFY(manager.autoHomeOnConnect());
        const auto binding = manager.bindingKey();
        const auto path = QSettings().fileName();
        const QByteArray corrupt("[broken\nthis is malformed\n");
        write(path, corrupt);
        QVERIFY(!manager.autoHomeOnConnect());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(!manager.autoHomeOnConnect());
        QCOMPARE(manager.bindingKey(), binding);
        // Qt can report the malformed cached INI during read or during its
        // later atomic sync. Both must fail off and retain the original bytes.
        QVERIFY2(manager.message().contains("nicht lesbar") ||
                     manager.message().contains("nicht gespeichert"),
                 qPrintable(manager.message()));
        QCOMPARE(read(path), corrupt);
        QVERIFY(m.simulation());
        QVERIFY(!m.requestPending());
    }
    void malformedIniIsRejectedBeforeSetterCanMutateSharedSettings() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("A", "synthetic")));
        const auto binding = manager.bindingKey();
        // The app cache saw a missing file during activation; replace the raw
        // file with malformed data and invoke the setter without a getter first.
        const auto path = QSettings().fileName();
        const QByteArray corrupt("[broken\nthis is malformed\n");
        write(path, corrupt);
        manager.setAutoHomeOnConnect(true);
        QVERIFY2(manager.message().contains("nicht lesbar"), qPrintable(manager.message()));
        QCOMPARE(read(path), corrupt);
        QVERIFY(!manager.autoHomeOnConnect());
        QCOMPARE(read(path), corrupt);
        QCOMPARE(manager.bindingKey(), binding);
        QVERIFY(m.simulation());
        QVERIFY(!m.requestPending());
        QVERIFY(!manager.connectionRequestPending());
    }
    void reentrantBindingMutationCannotSilentlySetPreference() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        const auto id = manager.addCurrent("A", "synthetic");
        // Make activation perform a real profile change. Its document signal
        // occurs inside Mutation, unlike the final manager.changed notification
        // which correctly announces the completed, editable state.
        auto json = d.deviceProfile().toJson();
        auto workspace = json["workspace"].toObject();
        workspace["width"] = 210.;
        json["workspace"] = workspace;
        QString error;
        const auto other = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY2(other.has_value(), qPrintable(error));
        QVERIFY(d.setDeviceProfile(*other));
        int attempts = 0;
        QString inFlightReason;
        const auto connection = connect(&d, &Document::deviceProfileChanged, &manager, [&] {
            ++attempts;
            manager.setAutoHomeOnConnect(true);
            inFlightReason = manager.message();
        });
        QVERIFY(manager.activate(id));
        disconnect(connection);
        QCOMPARE(attempts, 1);
        QVERIFY2(inFlightReason.contains("Geräteänderung läuft"), qPrintable(inFlightReason));
        QVERIFY(!manager.autoHomeOnConnect());
        // Once activation has finished, changing the next-connect preference is
        // ordinary allowed input; it still opens no endpoint or movement.
        manager.setAutoHomeOnConnect(true);
        QVERIFY(manager.autoHomeOnConnect());
        QVERIFY(!m.requestPending());
        QVERIFY(m.simulation());
    }
    void busyAndFiringSimulatorRejectChangesWithoutMotionRequests() {
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("A", "synthetic")));
        m.focus(true);
        QVERIFY(m.firing());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(!manager.autoHomeOnConnect());
        m.focus(false);
        // Simulator state fixture only; no physical session or motion grant involved.
        m.Simulator::frame({{"x", 5.}, {"y", 5.}, {"width", 5.}, {"height", 5.}, {"valid", true}});
        QVERIFY(m.busy());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(!manager.autoHomeOnConnect());
        m.stop();
        QVERIFY(m.simulation());
    }
    void pendingRejectsButActualConnectedIdleAllowsNextConnectPreference() {
        RuntimePeer peer;
        QVERIFY(peer.start(peerScript()).ok);
        QTemporaryDir t;
        Document d;
        d.setBackgroundPlanning(false);
        d.newProject();
        Machine m(&d);
        ConnectedCleanup cleanup{m};
        DeviceManager manager(&d, &m, t.filePath("devices.json"));
        QVERIFY(manager.activate(manager.addCurrent("Synthetic PTY", peer.port())));
        QVERIFY(!manager.autoHomeOnConnect());
        QVERIFY(manager.connectSelected());
        QVERIFY(manager.connectionRequestPending());
        manager.setAutoHomeOnConnect(true);
        QVERIFY(!manager.autoHomeOnConnect());
        QTRY_VERIFY(m.ready());
        QTRY_VERIFY(!manager.connectionRequestPending());
        QTRY_VERIFY(!m.occupied() && !m.requestPending());
        QVERIFY(!m.simulation());
        QVERIFY(m.connected());
        QVERIFY(!manager.changeBlockReason().isEmpty());
        const auto before = commands(peer);
        QSignalSpy opens(&m, &Machine::connectionRequestFinished);
        manager.setAutoHomeOnConnect(true);
        QVERIFY(manager.autoHomeOnConnect());
        QTest::qWait(80);
        QCOMPARE(commands(peer), before);
        QCOMPARE(opens.count(), 0);
        QVERIFY(m.connected());
        QVERIFY(!m.positionReferenced());
        noHome(peer);
        manager.setAutoHomeOnConnect(false);
        QVERIFY(!manager.autoHomeOnConnect());
        QCOMPARE(commands(peer), before);
        noHome(peer);
    }
};
int main(int argc, char **argv) {
    QTemporaryDir isolated;
    if (!isolated.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", isolated.filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", isolated.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath("cache").toUtf8());
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("BlazerPreferenceTests");
    DeviceAutoHomePreferenceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "deviceautohomepreference_test.moc"
