#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "support/runtimepeer.h"
#include <QtTest>
#include <QSemaphore>
#include <QJsonArray>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;
class ControllerRuntimeTestAccess {
public:
    static inline quint64 sequence=0;
    static bool action(ControllerRuntimeSession &r,ControllerNativeAction action,double x=0,double y=0) {
        const auto s=r.status();const auto now=RuntimePeer::monotonicNs()/1000000;
        const auto stamp=r.sealNativeAction(action,++sequence,now,now+250,ControllerNativeDigest::request(action,x,y),std::make_shared<NativeInputContextToken>(),s.connectionEpoch,s.coordinateEpoch);
        return r.submitNativeAction(action,stamp,x,y);
    }
    static bool start(ControllerRuntimeSession &r,PreparedControllerJob::Ptr job) {
        const auto s=r.status();const auto now=RuntimePeer::monotonicNs()/1000000;
        auto stamp=r.sealNativeAction(ControllerNativeAction::StartJob,++sequence,now,now+250,ControllerNativeDigest::request(ControllerNativeAction::StartJob,0,0,{},job->sourceFingerprint()+job->profileFingerprint()),std::make_shared<NativeInputContextToken>(),s.connectionEpoch,s.coordinateEpoch);
        return r.submitNativeAction(ControllerNativeAction::StartJob,stamp,0,0,{},std::move(job));
    }
    template<class F> static bool run(ControllerRuntimeSession &r,F fn) {
        auto done=std::make_shared<QSemaphore>();auto result=std::make_shared<std::atomic_bool>(false);
        if(!QMetaObject::invokeMethod(r.m_worker,[worker=r.m_worker,done,result,fn]{result->store(fn(*worker));done->release();},Qt::QueuedConnection))return false;
        return done->tryAcquire(1,3000)&&result->load();
    }
    static bool sensorUnknown(ControllerRuntimeSession&r){return run(r,[](ControllerRuntimeWorker &w){return w.m_accessory.state(AccessoryContract::Role::EnclosureExtraction).physical==AccessoryContract::PhysicalState::Unknown;});}
    static bool expire(ControllerRuntimeSession &r){return run(r,[](ControllerRuntimeWorker&w){w.m_commissioning.deadline=1;w.updateCommissioning();w.publish();return true;});}
};
class ControllerCommissioningTest:public QObject {
    Q_OBJECT
    const QString script=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py");
    static QString stage(ControllerRuntimeSession&r){return r.status().commissioning.value("stage").toString();}
    static int count(RuntimePeer&p,const QString&kind){return p.snapshot().object.value("counts").toObject().value(kind).toInt();}
    static bool data(ControllerRuntimeSession&r,const QString&op){return r.commissioningData({{"op",op}});}
    void startSetup(RuntimePeer &peer,ControllerRuntimeSession &r) {
        QVERIFY(peer.start(script).ok);QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(r.status().ready);
        QCOMPARE(count(peer,"physicalMove"),0);QVERIFY(data(r,"begin"));QTRY_COMPARE(stage(r),QString("mechanics"));
        QVERIFY(!r.status().motionSafetyError.isEmpty());
    }
    void home(RuntimePeer &peer,ControllerRuntimeSession&r) {
        startSetup(peer,r);if(QTest::currentTestFailed())return;
        QVERIFY(r.commissioningData({{"op","mechanics"},{"switches",true},{"direction",true},{"freeTravel",true},{"laserDisabled",true}}));QTRY_COMPARE(stage(r),QString("homeReady"));
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupHome));QTRY_COMPARE(stage(r),QString("geometry"));
        QVERIFY(data(r,"homeObserved"));QTRY_VERIFY(r.status().commissioning.value("homeObserved").toBool());
    }
    void geometry(RuntimePeer &peer,ControllerRuntimeSession&r) {
        home(peer,r);if(QTest::currentTestFailed())return;
        for(const auto step:{QPointF(1,0),QPointF(0,1)}) {
            QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupProbe,step.x(),step.y()));QTRY_COMPARE(stage(r),QString("probeConfirm"));
            QVERIFY(data(r,"probeObserved"));QTRY_COMPARE(stage(r),QString("geometry"));
        }
        QCOMPARE(r.status().commissioning.value("probesConfirmed").toInt(),2);
        QVERIFY(r.commissioningData({{"op","limits"},{"minX",0.0},{"minY",0.0},{"maxX",180.0},{"maxY",130.0}}));
        QTRY_VERIFY(r.status().commissioning.value("reason").toString().contains("überschreitet"));QCOMPARE(stage(r),QString("geometry"));
        QVERIFY(r.commissioningData({{"op","limits"},{"minX",-190.0},{"minY",-140.0},{"maxX",-10.0},{"maxY",-10.0}}));QTRY_COMPARE(stage(r),QString("cornerReady"));
    }
private slots:
    void fullSignedShiftedSession() {
        RuntimePeer peer;ControllerRuntimeSession r(DeviceProfile::m3Reference());geometry(peer,r);if(QTest::currentTestFailed())return;
        for(int i=0;i<4;++i) {
            QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupCorner,i));QTRY_COMPARE(stage(r),QString("cornerConfirm"));
            QVERIFY(data(r,"cornerObserved"));QTRY_COMPARE(stage(r),i==3?QString("workCoordinates"):QString("cornerReady"));
        }
        const auto plant=peer.snapshot().object;QCOMPARE(plant.value("actual").toArray(),QJsonArray({-190.0,-140.0}));
        QCOMPARE(count(peer,"collision"),0);QCOMPARE(count(peer,"powered"),0);
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupZero));QTRY_COMPARE(stage(r),QString("workCoordinates"));QTRY_VERIFY(std::abs(r.status().position.x())<0.001);
        QVERIFY(data(r,"coordinatesObserved"));QTRY_COMPARE(stage(r),QString("complete"));QVERIFY(r.status().motionSafetyError.isEmpty());
        QCOMPARE(r.status().commissioning.value("workMinX").toDouble(),0.0);QCOMPARE(r.status().commissioning.value("workMaxX").toDouble(),180.0);
        // Homing intentionally invalidates accessory knowledge. Never clear that
        // fault automatically merely because the geometry wizard completed.
        QVERIFY(r.operatorData({{"op","clearance"},{"confirmed",true}}));QTRY_VERIFY(r.status().extractionSupervision.value("reason").toString().startsWith("Sicheren Zustand"));
        QVERIFY(r.operatorData({{"op","select"}}));QTRY_COMPARE(r.status().extractionSupervision.value("mode").toString(),QString("operatorAttendedExternal"));
        QVERIFY(!r.status().extractionSafetyError.isEmpty());
        JobPlan plan;plan.moves.append({QPointF(1,1),QPointF(5,1),300,10,0,false});
        const auto prepared=PreparedControllerJob::prepare(plan);QVERIFY(prepared->valid());
        QVERIFY(ControllerRuntimeTestAccess::start(r,prepared));QTest::qWait(100);QCOMPARE(count(peer,"powered"),0);
        QVERIFY(r.operatorData({{"op","attest"},{"action","start"},{"source",QByteArray(32,'a')+prepared->profileFingerprint()},{"extractionOn",true},{"operatorPresent",true}}));
        QTRY_COMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString("start"));
        QVERIFY(ControllerRuntimeTestAccess::start(r,prepared));QTest::qWait(100);QCOMPARE(count(peer,"powered"),0);
        QTRY_VERIFY(r.status().extractionSupervision.value("pendingAction").toString().isEmpty());
        // Human statement is separate from a sensor and bound to this job attempt.
        QVERIFY(r.operatorData({{"op","attest"},{"action","start"},{"source",prepared->sourceFingerprint()+prepared->profileFingerprint()},{"extractionOn",true},{"operatorPresent",true}}));
        QTRY_COMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString("start"));
        QVERIFY(r.status().extractionSafetyError.isEmpty());
        QVERIFY(ControllerRuntimeTestAccess::start(r,prepared));QTRY_VERIFY(count(peer,"powered")>0);QTRY_VERIFY(!r.status().occupied);
        QCOMPARE(count(peer,"collision"),0);QCOMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString());
        const auto powered=count(peer,"powered");QVERIFY(ControllerRuntimeTestAccess::start(r,prepared));QTest::qWait(100);QCOMPARE(count(peer,"powered"),powered);
        QVERIFY(!r.status().overridesReady);
        QVERIFY(ControllerRuntimeTestAccess::sensorUnknown(r));
        const auto beforeFocus=count(peer,"physicalMove"),beforeFocusPower=count(peer,"powered");
        QVERIFY(r.operatorData({{"op","attest"},{"action","focus"},{"extractionOn",true},{"operatorPresent",true}}));
        QTRY_COMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString("focus"));
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::FocusHold));QTRY_VERIFY(count(peer,"powered")>beforeFocusPower);QTRY_VERIFY(!r.status().occupied);
        QCOMPARE(count(peer,"physicalMove"),beforeFocus);QVERIFY(ControllerRuntimeTestAccess::sensorUnknown(r));

        // Same active job identity survives a genuine Hold; the fresh Resume
        // witness is distinct and the existing holdConfirmed/native gate stays.
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);
        QVERIFY(r.operatorData({{"op","attest"},{"action","start"},{"source",prepared->sourceFingerprint()+prepared->profileFingerprint()},{"extractionOn",true},{"operatorPresent",true}}));
        QTRY_COMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString("start"));
        QVERIFY(ControllerRuntimeTestAccess::start(r,prepared));QTRY_VERIFY(r.status().accessoryLeaseActive);
        r.pause();QTRY_VERIFY(r.status().holdConfirmed);QVERIFY(!r.status().extractionSafetyError.isEmpty());
        QVERIFY(r.operatorData({{"op","attest"},{"action","resume"},{"extractionOn",true},{"operatorPresent",true}}));
        QTRY_COMPARE(r.status().extractionSupervision.value("pendingAction").toString(),QString("resume"));
        QVERIFY(r.status().extractionSafetyError.isEmpty());
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::Resume));QTRY_VERIFY(!r.status().paused);
        bool resumed=false;for(const auto &entry:peer.snapshot().object.value("events").toArray()){const auto e=entry.toObject();if(e.value("kind")=="realtime"&&e.value("value").toInt()==int('~'))resumed=true;}QVERIFY(resumed);

        r.stop();QTRY_COMPARE(stage(r),QString("fault"));QVERIFY(!r.status().positionReferenced);QVERIFY(!r.status().motionSafetyError.isEmpty());
        r.close();QTRY_VERIFY(!r.status().connected);
    }
    void missingAndNonemptyReadback_data(){QTest::addColumn<bool>("nonempty");QTest::newRow("missing-setting")<<false;QTest::newRow("nonempty-startup")<<true;}
    void missingAndNonemptyReadback(){QFETCH(bool,nonempty);RuntimePeer peer;QVERIFY(peer.start(script).ok);QVERIFY(peer.control(nonempty?QJsonObject{{"action","plantConfig"},{"startup","G0 X9"}}:QJsonObject{{"action","plantConfig"},{"omit",23}}).ok);
        ControllerRuntimeSession r(DeviceProfile::m3Reference());QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'b')).enqueued);QTRY_VERIFY(r.status().ready);QVERIFY(data(r,"begin"));QTRY_COMPARE(stage(r),QString("fault"));QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"homed"),0);r.close();QTRY_VERIFY(!r.status().connected);}
    void homingLockedAlarmCanBeCommissionedWithoutUnlock(){
        RuntimePeer peer;QVERIFY(peer.start(script).ok);QVERIFY(peer.control({{"action","plantConfig"},{"alarmAfterReset",true}}).ok);
        ControllerRuntimeSession r(DeviceProfile::m3Reference());QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'z')).enqueued);
        QTRY_VERIFY(r.status().statusFresh && !r.status().opening);QVERIFY(!r.status().ready);
        QVERIFY(data(r,"begin"));QTRY_COMPARE(stage(r),QString("mechanics"));
        QVERIFY(r.commissioningData({{"op","mechanics"},{"switches",true},{"direction",true},{"freeTravel",true},{"laserDisabled",true}}));QTRY_COMPARE(stage(r),QString("homeReady"));
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupHome));QTRY_VERIFY2(stage(r)=="geometry",qPrintable(r.status().commissioning.value("reason").toString()+" / "+r.logEntries().join(" | ")));
        QCOMPARE(count(peer,"homed"),1);
        const auto events=peer.snapshot().object.value("events").toArray();for(const auto &entry:events){const auto e=entry.toObject();QVERIFY(e.value("value").toString()!="$X");if(e.value("kind")=="realtime")QVERIFY(e.value("value").toInt()!=int('~'));}
        r.close();QTRY_VERIFY(!r.status().connected);
    }
    void noAttestationNoMovement(){RuntimePeer peer;ControllerRuntimeSession r(DeviceProfile::m3Reference());startSetup(peer,r);if(QTest::currentTestFailed())return;
        QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::SetupHome));QTest::qWait(100);QCOMPARE(stage(r),QString("mechanics"));QCOMPARE(count(peer,"homed"),0);
        QVERIFY(r.commissioningData({{"op","mechanics"},{"switches",true},{"direction",false},{"freeTravel",true},{"laserDisabled",true}}));QTest::qWait(80);QCOMPARE(stage(r),QString("mechanics"));r.close();QTRY_VERIFY(!r.status().connected);}
    void changedConfigurationRevokes(){RuntimePeer peer;ControllerRuntimeSession r(DeviceProfile::m3Reference());home(peer,r);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","reply"},{"data","$23=0\r\n"}}).ok);QTRY_COMPARE(stage(r),QString("fault"));QVERIFY(!r.status().positionReferenced);QVERIFY(!r.status().motionSafetyError.isEmpty());r.close();QTRY_VERIFY(!r.status().connected);}
    void absentAckTimeoutAndStop(){RuntimePeer peer;ControllerRuntimeSession r(DeviceProfile::m3Reference());QVERIFY(peer.start(script).ok);QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'c')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);QVERIFY(data(r,"begin"));QTRY_COMPARE(stage(r),QString("readback"));QVERIFY(ControllerRuntimeTestAccess::expire(r));QTRY_COMPARE(stage(r),QString("fault"));QCOMPARE(count(peer,"physicalMove"),0);r.close();QTRY_VERIFY(!r.status().connected);}
    void normalActionsCannotBorrowSetupEnvelope(){RuntimePeer peer;ControllerRuntimeSession r(DeviceProfile::m3Reference());home(peer,r);if(QTest::currentTestFailed())return;
        const auto moves=count(peer,"physicalMove");QVERIFY(ControllerRuntimeTestAccess::action(r,ControllerNativeAction::Jog,1,0));QTest::qWait(100);QCOMPARE(count(peer,"physicalMove"),moves);
        QVERIFY(!r.status().commissioning.value("ready").toBool());r.close();QTRY_VERIFY(!r.status().connected);}
};
QTEST_MAIN(ControllerCommissioningTest)
#include "controllercommissioning_test.moc"
