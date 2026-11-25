#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "controllercalibrationstore.h"
#include "connectionhomepermit.h"
#include "support/runtimepeer.h"
#include <QtTest>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;

// Private, explicitly synthetic native fixture. No OS/human-input or hardware
// claim: production input consumption remains in Machine's native adapter.
class ControllerRuntimeTestAccess {
public:
    static inline quint64 sequence=0;
    static bool native(ControllerRuntimeSession &runtime,ControllerNativeAction action,double x=0,double y=0) {
        const auto status=runtime.status();const auto now=RuntimePeer::monotonicNs()/1000000;
        const auto stamp=runtime.sealNativeAction(action,++sequence,now,now+250,
            ControllerNativeDigest::request(action,x,y),std::make_shared<NativeInputContextToken>(),status.connectionEpoch,status.coordinateEpoch);
        return runtime.submitNativeAction(action,stamp,x,y);
    }
    static void observe(ControllerRuntimeSession &runtime,QObject *context,QVariantMap *record) {
        QObject::connect(runtime.m_worker,&ControllerRuntimeWorker::calibrationCompleted,context,
            [record](QByteArray,QVariantMap value){*record=std::move(value);},Qt::QueuedConnection);
    }
    static bool alterHomePermit(ControllerRuntimeSession &runtime,const QString &change) {
        auto done=std::make_shared<QSemaphore>();auto changed=std::make_shared<std::atomic_bool>(false);
        if(!QMetaObject::invokeMethod(runtime.m_worker,[worker=runtime.m_worker,done,changed,change]{
            const auto permit=worker->m_connectionHomePermit;
            if(permit) {
                worker->m_connectionHomePermit=std::shared_ptr<const ConnectionHomePermit>(new ConnectionHomePermit(
                    permit->issuer,permit->interaction,permit->requestId,permit->generation,
                    permit->preOpenEpoch+(change=="permit-epoch"?1:0),
                    permit->observedAt-(change=="deadline"?20000:0),permit->admittedAt-(change=="deadline"?20000:0),
                    permit->endpoint,change=="permit-binding"?QByteArray(64,'d'):permit->binding,
                    change=="permit-profile"?QByteArray(64,'d'):permit->profileFingerprint,
                    change=="permit-record"?QByteArray(32,'d'):permit->recordDigest,permit->contextDigest));
                changed->store(true);
            }
            done->release();
        },Qt::QueuedConnection))return false;
        return done->tryAcquire(1,3000)&&changed->load();
    }
    static ControllerSubmission connectHome(ControllerRuntimeSession &runtime,const QString &endpoint,const QByteArray &binding,qint64 age=0,const QByteArray &overrideDigest={}) {
        const auto now=RuntimePeer::monotonicNs()/1000000;
        return runtime.requestOpenAndHome(endpoint,binding,++sequence,now-age,now-age+250,
            overrideDigest.isEmpty()?ControllerConnectionHomeDigest::context(endpoint,binding,runtime.deviceProfile().outputFingerprint()):overrideDigest);
    }
};

class AutoHomeConnectionTest : public QObject {
    Q_OBJECT
    const QByteArray binding=QByteArray(64,'a');
    const QString script=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/autohome_connection_peer.py");
    QTemporaryDir storage;
    QVariantMap record;
    static QString stage(ControllerRuntimeSession &runtime){return runtime.status().commissioning.value("stage").toString();}
    static int count(RuntimePeer &peer,const QString &kind){return peer.snapshot().object.value("counts").toObject().value(kind).toInt();}
    static int resetWrites(RuntimePeer &peer) {
        int resets=0;for(const auto &entry:peer.snapshot().object.value("events").toArray()) {
            const auto event=entry.toObject();
            if(event.value("kind")=="realtime"&&event.value("value").toInt()==0x18)++resets;
        }return resets;
    }
    static bool data(ControllerRuntimeSession &runtime,const QString &op){return runtime.commissioningData({{"op",op}});}
    static void noOutputOrApproach(RuntimePeer &peer){QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"coordinateUpdate"),0);QCOMPARE(count(peer,"collision"),0);}
private slots:
    void initTestCase();
    void init(){QString error;QVERIFY2(ControllerCalibrationStore().save(binding,record,&error),qPrintable(error));}
    void normalPublicConnectHasNoHome();
    void explicitlyRequestedMatchingHomeRunsOnce();
    void mismatchedOrMissingRecord_data();
    void mismatchedOrMissingRecord();
    void invalidInput_data();
    void invalidInput();
    void revocationBeforeHomeWrite_data();
    void revocationBeforeHomeWrite();
};

void AutoHomeConnectionTest::initTestCase() {
    QVERIFY(storage.isValid());

    qputenv("XDG_DATA_HOME",storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME",storage.path().toUtf8());
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    ControllerRuntimeTestAccess::observe(runtime,this,&record);
    QVERIFY(peer.start(script).ok);
    QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);
    QTRY_VERIFY(runtime.status().ready);
    if(stage(runtime)!="mechanics") {QVERIFY(data(runtime,"begin"));QTRY_COMPARE(stage(runtime),QString("mechanics"));}
    QVERIFY(runtime.commissioningData({{"op","mechanics"},{"switches",true},{"direction",true},{"freeTravel",true},{"laserDisabled",true}}));
    QTRY_COMPARE(stage(runtime),QString("homeReady"));
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupHome));
    QTRY_COMPARE(stage(runtime),QString("geometry"));
    QVERIFY(data(runtime,"homeObserved"));QTRY_VERIFY(runtime.status().commissioning.value("homeObserved").toBool());
    for(const auto point:{QPointF(1,0),QPointF(0,1)}) {
        QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupProbe,point.x(),point.y()));
        QTRY_COMPARE(stage(runtime),QString("probeConfirm"));QVERIFY(data(runtime,"probeObserved"));
        QTRY_COMPARE(stage(runtime),point.y()!=0?QString("limits"):QString("geometry"));
    }
    QVERIFY(runtime.commissioningData({{"op","limits"},{"minX",-190.},{"minY",-140.},{"maxX",-10.},{"maxY",-10.}}));
    QTRY_COMPARE(stage(runtime),QString("cornerReady"));
    for(int index=0;index<4;++index) {
        QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupCorner,index));
        QTRY_COMPARE(stage(runtime),QString("cornerConfirm"));QVERIFY(data(runtime,"cornerObserved"));
        QTRY_COMPARE(stage(runtime),index==3?QString("workCoordinates"):QString("cornerReady"));
    }
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupZero));
    QTRY_VERIFY(stage(runtime)=="workCoordinates" && !runtime.status().coordinateUpdatePending && !runtime.status().requestPending);
    QVERIFY(data(runtime,"coordinatesObserved"));QTRY_COMPARE(stage(runtime),QString("complete"));
    QTRY_VERIFY(!record.isEmpty());QCOMPARE(count(peer,"collision"),0);QCOMPARE(count(peer,"powered"),0);
    // The production queued calibrationCompleted receiver, not the test, writes
    // the authenticated store. Wait for that independent queued side effect.
    ControllerCalibrationStore store;QString error;QVariantMap persisted;
    const auto saved=[&]{persisted=store.load(binding,&error);return !persisted.isEmpty();};
    QTRY_VERIFY2(saved(),qPrintable(error));QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(persisted,record);
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}

void AutoHomeConnectionTest::normalPublicConnectHasNoHome() {
    RuntimePeer peer;QVERIFY(peer.start(script).ok);ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));
    QCOMPARE(count(peer,"homed"),0);noOutputOrApproach(peer);QVERIFY(!runtime.status().positionReferenced);
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
void AutoHomeConnectionTest::explicitlyRequestedMatchingHomeRunsOnce() {
    RuntimePeer peer;QVERIFY(peer.start(script).ok);ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    QVERIFY(ControllerRuntimeTestAccess::connectHome(runtime,peer.port(),binding).enqueued);
    QTRY_COMPARE(stage(runtime),QString("savedApproachReady"));QCOMPARE(count(peer,"homed"),1);
    // Even after fresh status updates the connection intent cannot become an
    // automatic approach, a repeat Home or an output/job intent.
    const auto queries=count(peer,"query");QTRY_VERIFY(count(peer,"query")>=queries+3);
    QCOMPARE(count(peer,"homed"),1);noOutputOrApproach(peer);
    QVERIFY(!runtime.status().commissioning.value("ready").toBool());
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
void AutoHomeConnectionTest::mismatchedOrMissingRecord_data() {
    QTest::addColumn<QString>("change");
    for(const auto name:{"missing","wrong-binding","profile","settings","live-settings","identity"})QTest::newRow(name)<<QString::fromLatin1(name);
}
void AutoHomeConnectionTest::mismatchedOrMissingRecord() {
    QFETCH(QString,change);auto candidate=record;auto device=binding;
    if(change=="missing")device=QByteArray(64,'b');
    if(change=="wrong-binding")candidate["binding"]=QString(64,'b');
    if(change=="profile")candidate["profileFingerprint"]=QString(64,'d');
    if(change=="settings")candidate["settingsFingerprint"]=QString(64,'d');
    if(change=="identity")candidate["deviceIdentityHash"]=QString(64,'d');
    if(candidate!=record){QString error;QVERIFY2(ControllerCalibrationStore().save(binding,candidate,&error),qPrintable(error));}
    RuntimePeer peer;QVERIFY(peer.start(script).ok);
    if(change=="live-settings")QVERIFY(peer.control({{"action","plantConfig"},{"setting",QJsonArray{100,81.}}}).ok);
    ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    QVERIFY(ControllerRuntimeTestAccess::connectHome(runtime,peer.port(),device).enqueued);
    QTRY_COMPARE(stage(runtime),QString("mechanics"));
    QCOMPARE(count(peer,"homed"),0);noOutputOrApproach(peer);QVERIFY(!runtime.status().positionReferenced);
    QVERIFY(!runtime.status().commissioning.value("ready").toBool());
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
void AutoHomeConnectionTest::invalidInput_data() {
    QTest::addColumn<QString>("invalid");for(const auto name:{"stale-press","future-press","wrong-context"})QTest::newRow(name)<<QString::fromLatin1(name);
}
void AutoHomeConnectionTest::invalidInput() {
    QFETCH(QString,invalid);RuntimePeer peer;QVERIFY(peer.start(script).ok);ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    const auto submission=ControllerRuntimeTestAccess::connectHome(runtime,peer.port(),binding,invalid=="stale-press"?251:invalid=="future-press"?-100:0,invalid=="wrong-context"?QByteArray(32,'x'):QByteArray());
    QVERIFY(!submission.enqueued);QVERIFY(!runtime.status().positionReferenced);QCOMPARE(count(peer,"homed"),0);noOutputOrApproach(peer);
}
void AutoHomeConnectionTest::revocationBeforeHomeWrite_data() {
    QTest::addColumn<QString>("loss");for(const auto name:{"stop","close","deadline","reset","settings","permit-epoch","permit-binding","permit-profile","permit-record"})QTest::newRow(name)<<QString::fromLatin1(name);
}
void AutoHomeConnectionTest::revocationBeforeHomeWrite() {
    QFETCH(QString,loss);RuntimePeer peer;QVERIFY(peer.start(script).ok);QVERIFY(peer.control({{"action","holdHomeM5"}}).ok);
    ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    QVERIFY(ControllerRuntimeTestAccess::connectHome(runtime,peer.port(),binding).enqueued);
    QTRY_COMPARE(count(peer,"homeM5AckHeld"),1);QCOMPARE(count(peer,"homed"),0);noOutputOrApproach(peer);
    const auto resetsBeforeRevocation=resetWrites(peer);
    if(loss=="stop")runtime.stop();
    if(loss=="close")runtime.close();
    if(loss=="reset")QVERIFY(peer.control({{"action","reply"},{"data","Grbl 1.3a ['$' for help]\r\n"}}).ok);
    if(loss=="settings")QVERIFY(peer.control({{"action","reply"},{"data","$100=81\r\n"}}).ok);
    if(loss=="deadline"||loss.startsWith("permit-"))QVERIFY(ControllerRuntimeTestAccess::alterHomePermit(runtime,loss));
    // Delayed firmware ACK must not resurrect the already revoked intent.
    QVERIFY(peer.control({{"action","releaseHomeM5"}}).ok);
    if(loss=="close")QTRY_VERIFY(!runtime.status().connected);
    else if(loss=="deadline") {
        // The watchdog may consume the expired permit before the held M5 ACK
        // reaches the $H write hook. Both paths need their own concrete proof.
        const auto rejected=[&] {
            const auto status=runtime.status();
            if(status.requestPending)return false;
            const bool atWrite=!status.connected && runtime.logEntries().join("\n").contains("Verbindungsautorisierung");
            const bool watchdog=status.commissioning.value("stage")=="fault" &&
                status.commissioning.value("reason").toString().contains("15-Sekunden-Verbindungsfrist") &&
                resetWrites(peer)>resetsBeforeRevocation;
            return atWrite||watchdog;
        };
        QTRY_VERIFY2(rejected(),qPrintable(runtime.logEntries().join("\n")));
    } else if(loss.startsWith("permit-")) {
        // At-write authorization denial deliberately calls GrblSession::fail:
        // polling stops and the port closes. More status queries would be the
        // wrong oracle for this terminal, safely rejected operation.
        QTRY_VERIFY(!runtime.status().connected);
        QVERIFY2(runtime.logEntries().join("\n").contains("Verbindungsautorisierung"),qPrintable(runtime.logEntries().join("\n")));
        QTRY_VERIFY(!runtime.status().requestPending);
    } else if(loss=="settings") {
        // A changed readback revokes commissioning; its stale queued ACK may
        // cause a terminal transport fault rather than a new polling cycle.
        QTRY_COMPARE(stage(runtime),QString("fault"));
        QTRY_VERIFY(!runtime.status().requestPending);
        QVERIFY2(runtime.status().commissioning.value("reason").toString().contains("verändert") || !runtime.status().connected,
                 qPrintable(runtime.status().commissioning.value("reason").toString()));
    } else {const auto queries=count(peer,"query");QTRY_VERIFY(count(peer,"query")>=queries+3);}
    QCOMPARE(count(peer,"homed"),0);noOutputOrApproach(peer);QVERIFY(!runtime.status().positionReferenced);
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
QTEST_MAIN(AutoHomeConnectionTest)
#include "autohomeconnection_test.moc"
