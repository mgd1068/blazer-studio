#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "controllercalibrationstore.h"
#include "preparedcontrollerjob.h"
#include "job.h"
#include "support/runtimepeer.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonArray>
using RuntimeTestSupport::RuntimePeer;

// Explicitly synthetic private native fixture: it is not an OS input proof and
// can reach only this test's independently created and provenance-checked PTY.
class ControllerRuntimeTestAccess {
public:
    static inline quint64 sequence=0;
    static void ordinaryFocusRelease(ControllerRuntimeSession &runtime){runtime.revokeFocusContext();}
    static void globalContextLoss(ControllerRuntimeSession &runtime){runtime.invalidatePendingSequenceContext();}
    static void sourceEdit(ControllerRuntimeSession &runtime){runtime.cancelPendingStartSequence();}
    static bool action(ControllerRuntimeSession &runtime,ControllerNativeAction action,double x=0,double y=0,PreparedControllerJob::Ptr prepared={}) {
        const auto status=runtime.status();const auto now=RuntimePeer::monotonicNs()/1000000;
        const auto source=prepared?prepared->sourceFingerprint()+prepared->profileFingerprint():QByteArray();
        const auto stamp=runtime.sealNativeAction(action,++sequence,now,now+250,
            ControllerNativeDigest::request(action,x,y,{},source),std::make_shared<NativeInputContextToken>(),status.connectionEpoch,status.coordinateEpoch);
        return runtime.submitNativeAction(action,stamp,x,y,{},std::move(prepared));
    }
};
class StartAttendedSequenceTest:public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    int serial=0;
    const QByteArray binding=QByteArray(64,'a');
    const QString script=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/start_home_return_peer.py");
    static QString stage(ControllerRuntimeSession &runtime){return runtime.status().commissioning.value("stage").toString();}
    static int count(RuntimePeer &peer,const QString &kind){return peer.snapshot().object.value("counts").toObject().value(kind).toInt();}
    static bool data(ControllerRuntimeSession &runtime,const QString &op){return runtime.commissioningData({{"op",op}});}
    static PreparedControllerJob::Ptr job() {
        JobPlan plan;plan.moves={{QPointF(10,20),QPointF(30,40),300,0,0,true},
                               {QPointF(30,40),QPointF(40,40),300,10,0,false}};
        return PreparedControllerJob::prepare(plan);
    }
    void startPeer(RuntimePeer &peer,bool shifted) {
        QVERIFY(peer.start(script).ok);QVERIFY(peer.control({{"action","plantConfig"},{"positiveMachineSpace",true}}).ok);
        if(shifted)QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"offset",QJsonArray{-10.,-20.}}}}}).ok);
    }
    void calibrate(RuntimePeer &peer,ControllerRuntimeSession &runtime,bool shifted) {
        startPeer(peer,shifted);if(QTest::currentTestFailed())return;
        QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);QTRY_COMPARE(stage(runtime),QString("mechanics"));
        QVERIFY(runtime.commissioningData({{"op","mechanics"},{"switches",true},{"direction",true},{"freeTravel",true},{"laserDisabled",true}}));
        QTRY_COMPARE(stage(runtime),QString("homeReady"));
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::SetupHome));QTRY_COMPARE(stage(runtime),QString("geometry"));
        QVERIFY(data(runtime,"homeObserved"));QTRY_VERIFY(runtime.status().commissioning.value("homeObserved").toBool());
        for(const auto point:{QPointF(1,0),QPointF(0,1)}) {
            QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::SetupProbe,point.x(),point.y()));QTRY_COMPARE(stage(runtime),QString("probeConfirm"));
            QVERIFY(data(runtime,"probeObserved"));QTRY_COMPARE(stage(runtime),point.y()!=0?QString("limits"):QString("geometry"));
        }
        QVERIFY(runtime.commissioningData({{"op","limits"},{"minX",0.},{"minY",0.},{"maxX",180.},{"maxY",130.}}));QTRY_COMPARE(stage(runtime),QString("cornerReady"));
        for(int i=0;i<4;++i) {
            QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::SetupCorner,i));QTRY_COMPARE(stage(runtime),QString("cornerConfirm"));
            QVERIFY(data(runtime,"cornerObserved"));QTRY_COMPARE(stage(runtime),i==3?QString("workCoordinates"):QString("cornerReady"));
        }
        // Preserve shifted G54: no G10 convenience zero can mask a G53/G54 bug.
        QVERIFY(data(runtime,"coordinatesObserved"));QTRY_COMPARE(stage(runtime),QString("complete"));
        QString error;QVariantMap saved;
        const auto persisted=[&]{saved=ControllerCalibrationStore().load(binding,&error);return !saved.isEmpty();};
        QTRY_VERIFY2(persisted(),qPrintable(error));QVERIFY(error.isEmpty());
        QCOMPARE(saved.value("homeX").toDouble(),0.);QCOMPARE(saved.value("homeY").toDouble(),0.);
        QCOMPARE(saved.value("workOriginX").toDouble(),shifted?-10.:0.);QCOMPARE(saved.value("workOriginY").toDouble(),shifted?-20.:0.);
        QCOMPARE(count(peer,"collision"),0);QCOMPARE(count(peer,"powered"),0);
    }
    void saveFixtureCalibration(bool shifted) {
        RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());calibrate(peer,runtime,shifted);
        runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    static QJsonArray eventsAfter(RuntimePeer &peer,qint64 since) {
        QJsonArray result;for(const auto &entry:peer.snapshot().object.value("events").toArray())
            if(entry.toObject().value("atNs").toInteger()>=since)result.append(entry);
        return result;
    }
private slots:
    void init(){QVERIFY(storage.isValid());const auto path=storage.filePath(QString::number(++serial));QVERIFY(QDir().mkpath(path));qputenv("XDG_DATA_HOME",path.toUtf8());qputenv("XDG_CONFIG_HOME",path.toUtf8());}
    void peerG53IsNonmodalAndIgnoresG54() {
        RuntimePeer peer;startPeer(peer,true);if(QTest::currentTestFailed())return;
        auto command=[&](QString line){const auto reply=peer.control({{"action","fixtureCommand"},{"line",line}});return reply.ok&&reply.object.value("accepted").toBool();};
        QVERIFY(command("$H"));QVERIFY(command("G53 G1 X5 Y6 F600"));
        QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({5.,6.}));
        // Without another G53, ordinary G1 must again use WCO(-10,-20).
        QVERIFY(command("G1 X20 Y30 F600"));
        QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({10.,10.}));
        QCOMPARE(count(peer,"machineCoordinateMove"),1);QCOMPARE(count(peer,"collision"),0);
        for(const auto &line:{"G53 G0 G1 X1 Y1","G53 G1 G91 X1 Y1","G53 G1 Z1","G53.1 G1 X1 Y1","G53 G1 X1 X2 Y1","G53 X1 Y1"}) {
            QVERIFY2(!command(line),line);
            QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({10.,10.}));
        }
    }
    void referenceHomeNeedsNoPreparedJob_data() {
        QTest::addColumn<bool>("freshConnection");QTest::newRow("reference-from-complete")<<false;QTest::newRow("reference-from-saved-home-ready")<<true;
    }
    void referenceHomeNeedsNoPreparedJob() {
        QFETCH(bool,freshConnection);
        if(freshConnection){saveFixtureCalibration(true);if(QTest::currentTestFailed())return;}
        RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
        if(freshConnection){startPeer(peer,true);QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));}
        else calibrate(peer,runtime,true);
        if(QTest::currentTestFailed())return;
        QTRY_VERIFY2(runtime.status().extractionSupervision.value("referenceReady").toBool(),qPrintable(runtime.status().extractionSupervision.value("referenceError").toString()));
        const auto homes=count(peer,"homed"),moves=count(peer,"physicalMove"),parks=count(peer,"machineCoordinateMove");
        const auto since=RuntimePeer::monotonicNs();
        // Deliberately no PreparedControllerJob: ReferenceHome is its own fresh
        // native action and can never inherit a hidden Start/job continuation.
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::ReferenceHome));
        QTRY_COMPARE(count(peer,"homed"),homes+1);
        QTRY_VERIFY(!runtime.status().occupied&&!runtime.status().busy);
        QVERIFY(!runtime.status().extractionSupervision.value("referencePending").toBool());
        QCOMPARE(count(peer,"physicalMove"),moves);QCOMPARE(count(peer,"machineCoordinateMove"),parks);QCOMPARE(count(peer,"powered"),0);
        QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({0.,0.}));
        for(const auto &entry:eventsAfter(peer,since)) {
            const auto event=entry.toObject();if(event.value("kind")!="command")continue;
            const auto line=event.value("value").toString();
            QVERIFY2(!line.startsWith("G10")&&!line.contains('X')&&!line.contains('Y'),qPrintable(line));
        }
        QCOMPARE(count(peer,"homed"),homes+1);QVERIFY(!runtime.status().accessoryJobActive);QVERIFY(!runtime.status().accessoryFocusActive);
        runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    void successfulHomeJobReturn_data(){QTest::addColumn<bool>("shifted");QTest::addColumn<bool>("freshConnection");QTest::newRow("complete-g54-zero")<<false<<false;QTest::newRow("complete-g54-shifted")<<true<<false;QTest::newRow("saved-readback-g54-zero")<<false<<true;QTest::newRow("saved-readback-g54-shifted")<<true<<true;}
    void successfulHomeJobReturn() {
        QFETCH(bool,shifted);QFETCH(bool,freshConnection);
        if(freshConnection){saveFixtureCalibration(shifted);if(QTest::currentTestFailed())return;}
        RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
        if(freshConnection){startPeer(peer,shifted);QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));}
        else calibrate(peer,runtime,shifted);
        if(QTest::currentTestFailed())return;
        const auto prepared=job();QVERIFY2(prepared->valid(),qPrintable(prepared->errors().join(";")));
        const auto homes=count(peer,"homed"),parks=count(peer,"machineCoordinateMove");const auto since=RuntimePeer::monotonicNs();
        QVERIFY(peer.control({{"action","startSequenceFixture"},{"holdParkM5",true}}).ok);
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::StartAttended,0,0,prepared));
        QTRY_COMPARE(count(peer,"homed"),homes+1);QTRY_VERIFY(count(peer,"powered")>0);
        QTRY_COMPARE(count(peer,"parkM5AckHeld"),1);QCOMPARE(count(peer,"machineCoordinateMove"),parks);
        QVERIFY(runtime.status().occupied||runtime.status().busy);
        QVERIFY(peer.control({{"action","releaseParkM5"}}).ok);
        QTRY_COMPARE(count(peer,"machineCoordinateMove"),parks+1);QTRY_VERIFY(!runtime.status().occupied&&!runtime.status().busy);
        QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({0.,0.}));QCOMPARE(count(peer,"collision"),0);
        int homeIndex=-1,powerIndex=-1,m5ReleaseIndex=-1,parkIndex=-1,index=0;
        for(const auto &entry:eventsAfter(peer,since)) {
            const auto event=entry.toObject();const auto kind=event.value("kind").toString();
            if(kind=="homed"&&homeIndex<0)homeIndex=index;
            if(kind=="powered"&&powerIndex<0)powerIndex=index;
            if(kind=="parkM5AckReleased")m5ReleaseIndex=index;
            if(kind=="machineCoordinateMove") {parkIndex=index;const auto value=event.value("value").toObject();QCOMPARE(value.value("spindle").toInt(),5);QCOMPARE(value.value("power").toDouble(),0.);QCOMPARE(value.value("reported").toArray(),QJsonArray({0.,0.}));}
            if(kind=="command"){const auto line=event.value("value").toString();QVERIFY(!line.startsWith("G10"));QVERIFY(line!="$X");}
            ++index;
        }
        QVERIFY(homeIndex>=0);QVERIFY(powerIndex>homeIndex);QVERIFY(m5ReleaseIndex>powerIndex);QVERIFY(parkIndex>m5ReleaseIndex);
        QCOMPARE(count(peer,"homed"),homes+1);runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    void missingOrImpossibleCalibrationCannotStart_data(){QTest::addColumn<bool>("impossibleHome");QTest::newRow("missing-calibration")<<false;QTest::newRow("home-outside-plausible-machine-travel")<<true;}
    void missingOrImpossibleCalibrationCannotStart() {
        QFETCH(bool,impossibleHome);
        if(impossibleHome) {
            saveFixtureCalibration(false);if(QTest::currentTestFailed())return;
            QString error;auto record=ControllerCalibrationStore().load(binding,&error);QVERIFY(!record.isEmpty());
            record["homeX"]=500.;record["observedMaxX"]=500.;
            QVERIFY2(ControllerCalibrationStore().save(binding,record,&error),qPrintable(error));
        }
        RuntimePeer peer;startPeer(peer,false);ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
        QVERIFY(runtime.requestOpen(peer.port(),binding).enqueued);QTRY_COMPARE(stage(runtime),QString("mechanics"));
        const auto prepared=job();QVERIFY(prepared->valid());
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::StartAttended,0,0,prepared));
        QTRY_VERIFY(!runtime.status().requestPending&&!runtime.status().busy);
        const auto queries=count(peer,"query");QTRY_VERIFY(count(peer,"query")>=queries+3);
        QCOMPARE(count(peer,"homed"),0);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"machineCoordinateMove"),0);
        QVERIFY(!runtime.status().positionReferenced);runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    void stopDuringHomeDoesNotStartFrozenJob() {
        RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());calibrate(peer,runtime,false);if(QTest::currentTestFailed())return;
        const auto homes=count(peer,"homed"),parks=count(peer,"machineCoordinateMove");
        QVERIFY(peer.control({{"action","holdHomeM5"}}).ok);const auto prepared=job();QVERIFY(prepared->valid());
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::StartAttended,0,0,prepared));
        QTRY_COMPARE(count(peer,"homeM5AckHeld"),1);QCOMPARE(count(peer,"homed"),homes);
        QVERIFY(runtime.status().extractionSupervision.value("startSequencePending").toBool());
        QCOMPARE(runtime.status().extractionSupervision.value("startSequencePhase").toString(),QString("homing"));
        runtime.stop();QTRY_VERIFY(!runtime.status().positionReferenced);QTRY_VERIFY(!runtime.status().requestPending);
        QVERIFY(peer.control({{"action","releaseHomeM5"}}).ok);
        const auto queries=count(peer,"query");QTRY_VERIFY(!runtime.status().connected||count(peer,"query")>=queries+3);
        QCOMPARE(count(peer,"homed"),homes);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"machineCoordinateMove"),parks);
        QVERIFY(!runtime.status().extractionSupervision.value("startSequencePending").toBool());
        runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    void pendingSequenceContext_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("focus-release-preserves-reference")<<QString("focus-release");
        QTest::newRow("window-loss-revokes-reference")<<QString("window-loss");
        QTest::newRow("source-edit-revokes-frozen-start")<<QString("source-edit");
    }
    void pendingSequenceContext() {
        QFETCH(QString,change);RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
        calibrate(peer,runtime,false);if(QTest::currentTestFailed())return;
        const auto homes=count(peer,"homed"),moves=count(peer,"physicalMove"),parks=count(peer,"machineCoordinateMove");
        QVERIFY(peer.control({{"action","holdHomeM5"}}).ok);
        const bool reference=change!="source-edit";
        const auto prepared=reference?PreparedControllerJob::Ptr():job();
        if(!reference)QVERIFY(prepared->valid());
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,reference?ControllerNativeAction::ReferenceHome:ControllerNativeAction::StartAttended,0,0,prepared));
        QTRY_COMPARE(count(peer,"homeM5AckHeld"),1);QCOMPARE(count(peer,"homed"),homes);
        QVERIFY(runtime.status().extractionSupervision.value(reference?"referencePending":"startSequencePending").toBool());
        if(change=="focus-release")ControllerRuntimeTestAccess::ordinaryFocusRelease(runtime);
        if(change=="window-loss")ControllerRuntimeTestAccess::globalContextLoss(runtime);
        if(change=="source-edit")ControllerRuntimeTestAccess::sourceEdit(runtime);
        // Release immediately after the synchronous context fence, so the next
        // $H write cannot rely solely on a later watchdog cancellation tick.
        QVERIFY(peer.control({{"action","releaseHomeM5"}}).ok);
        if(change=="focus-release") {
            QTRY_COMPARE(count(peer,"homed"),homes+1);
            QTRY_VERIFY(!runtime.status().occupied&&!runtime.status().busy);
            QVERIFY(runtime.status().positionReferenced);
        } else {
            QTRY_VERIFY(!runtime.status().extractionSupervision.value(reference?"referencePending":"startSequencePending").toBool());
            QTRY_VERIFY(!runtime.status().requestPending);QVERIFY(!runtime.status().positionReferenced);
            const auto queries=count(peer,"query");QTRY_VERIFY(!runtime.status().connected||count(peer,"query")>=queries+3);
            QCOMPARE(count(peer,"homed"),homes);
        }
        QCOMPARE(count(peer,"physicalMove"),moves);QCOMPARE(count(peer,"machineCoordinateMove"),parks);QCOMPARE(count(peer,"powered"),0);
        runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
    void interruptionNeverParksOrRestarts_data(){QTest::addColumn<QString>("loss");QTest::newRow("stop")<<QString("stop");QTest::newRow("alarm")<<QString("alarm");QTest::newRow("door")<<QString("door");}
    void interruptionNeverParksOrRestarts() {
        QFETCH(QString,loss);RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());calibrate(peer,runtime,true);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","startSequenceFixture"},{"holdPoweredAck",true}}).ok);
        const auto prepared=job();QVERIFY(prepared->valid());const auto homes=count(peer,"homed"),parks=count(peer,"machineCoordinateMove");
        QVERIFY(ControllerRuntimeTestAccess::action(runtime,ControllerNativeAction::StartAttended,0,0,prepared));QTRY_COMPARE(count(peer,"poweredAckHeld"),1);QTRY_VERIFY(count(peer,"powered")>0);
        if(loss=="stop")runtime.stop();
        if(loss=="alarm")QVERIFY(peer.control({{"action","reply"},{"data","ALARM:1\r\n"}}).ok);
        if(loss=="door"){QVERIFY(peer.control({{"action","plantConfig"},{"doorSubstate",1}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);}
        QTRY_VERIFY(!runtime.status().positionReferenced);QTRY_VERIFY(!runtime.status().requestPending);
        const auto powerAfterRevocation=count(peer,"powered");
        QVERIFY(peer.control({{"action","releasePoweredAck"}}).ok);
        const auto queries=count(peer,"query");QTRY_VERIFY(!runtime.status().connected||count(peer,"query")>=queries+3);
        QCOMPARE(count(peer,"homed"),homes+1);QCOMPARE(count(peer,"machineCoordinateMove"),parks);QCOMPARE(count(peer,"powered"),powerAfterRevocation);QVERIFY(!runtime.status().firing);
        runtime.close();QTRY_VERIFY(!runtime.status().connected);
    }
};
QTEST_MAIN(StartAttendedSequenceTest)
#include "startattendedsequence_test.moc"
