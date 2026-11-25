#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "controllercalibrationstore.h"
#include "support/runtimepeer.h"
#include <QtTest>
#include <QJsonArray>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <limits>

using RuntimeTestSupport::RuntimePeer;

// The record transport is a private worker seam. This test deliberately does not
// mint authenticated store files or claim that a persisted record is a reference.
class ControllerRuntimeTestAccess {
public:
    static inline quint64 sequence = 0;
    static bool native(ControllerRuntimeSession &runtime, ControllerNativeAction action, double x=0, double y=0) {
        const auto status=runtime.status(); const auto now=RuntimePeer::monotonicNs()/1000000;
        const auto stamp=runtime.sealNativeAction(action,++sequence,now,now+250,
            ControllerNativeDigest::request(action,x,y),std::make_shared<NativeInputContextToken>(),
            status.connectionEpoch,status.coordinateEpoch);
        return runtime.submitNativeAction(action,stamp,x,y);
    }
    static void observe(ControllerRuntimeSession &runtime,QObject *context,QVariantMap *record) {
        QObject::connect(runtime.m_worker,&ControllerRuntimeWorker::calibrationCompleted,context,
            [record](QByteArray,QVariantMap value){*record=std::move(value);},Qt::QueuedConnection);
    }
    static bool open(ControllerRuntimeSession &runtime,const QString &endpoint,const QByteArray &binding,const QVariantMap &record) {
        ControllerRuntimeRequest request(ControllerRuntimeRequestKind::Open);
        request.id=++runtime.m_nextRequest;request.endpoint=endpoint;request.binding=binding;
        request.calibrationRecord=record;request.issuedAt=RuntimePeer::monotonicNs()/1000000;
        request.expiresAt=request.issuedAt+3000;
        if(!runtime.m_mailbox->enqueue(std::move(request)))return false;
        runtime.m_pendingOpen=runtime.m_nextRequest;
        return true;
    }
};

class CalibrationReconnectTest : public QObject {
    Q_OBJECT
    const QByteArray binding=QByteArray(64,'a');
    QVariantMap record;
    QTemporaryDir storage;
    QString script;
    static QString stage(ControllerRuntimeSession &runtime) {return runtime.status().commissioning.value("stage").toString();}
    static int count(RuntimePeer &peer,const QString &name) {return peer.snapshot().object.value("counts").toObject().value(name).toInt();}
    static QStringList commands(RuntimePeer &peer) {
        QStringList result;for(const auto &entry:peer.snapshot().object.value("events").toArray()) {
            const auto object=entry.toObject();if(object.value("kind")=="command")result<<object.value("value").toString();
        }return result;
    }
    static bool data(ControllerRuntimeSession &runtime,const QString &operation) {return runtime.commissioningData({{"op",operation}});}
    static void noMotion(RuntimePeer &peer) {
        QCOMPARE(count(peer,"homed"),0);QCOMPARE(count(peer,"physicalMove"),0);
        QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"coordinateUpdate"),0);
        for(const auto &command:commands(peer)) {
            QVERIFY2(command!="$H" && command!="$X" && !command.startsWith("G10"),qPrintable(command));
            QVERIFY2(!QRegularExpression("^\\$[0-9]+=.*").match(command).hasMatch(),qPrintable(command));
        }
    }
    QString identifiedScript(const QString &identity) {
        const auto path=storage.filePath("identified-"+QString::number(++scriptSequence)+".py");
        QFile wrapper(path);if(!wrapper.open(QIODevice::WriteOnly))return {};
        const auto support=QJsonDocument(QJsonArray{QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support")}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
        wrapper.write("import sys\nsys.path.insert(0,"+support+")\nfrom m3_commissioning_peer import Plant\nclass IdentifiedPlant(Plant):\n def command(self,line):\n  if line=='$I': self.reply('[MSG:synthetic:M3:MAC="+identity.toLatin1()+"]\\r\\n')\n  super().command(line)\np=IdentifiedPlant()\ntry:p.run()\nfinally:p.close()\n");
        return path;
    }
    int scriptSequence=0;
    void connectRecord(RuntimePeer &peer,ControllerRuntimeSession &runtime,const QByteArray &device,const QVariantMap &candidate,const QJsonObject &configuration={},const QString &peerScript={}) {
        QVERIFY(peer.start(peerScript.isEmpty()?script:peerScript).ok);
        if(!configuration.isEmpty())QVERIFY(peer.control(configuration).ok);
        QVERIFY(ControllerRuntimeTestAccess::open(runtime,peer.port(),device,candidate));
    }
private slots:
    void initTestCase();
    void persistedRecordThroughPublicOpen();
    void connectIsReadbackOnly();
    void freshHomeAndSeparateApproach_data();
    void freshHomeAndSeparateApproach();
    void changedRecord_data();
    void changedRecord();
    void identity_data();
    void identity();
    void revocation_data();
    void revocation();
};

void CalibrationReconnectTest::initTestCase() {
    QVERIFY(storage.isValid());
    script=identifiedScript("02:11:22:33:44:55");QVERIFY(!script.isEmpty());
    qputenv("XDG_DATA_HOME",storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME",storage.path().toUtf8());
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    ControllerRuntimeTestAccess::observe(runtime,this,&record);
    connectRecord(peer,runtime,binding,{});if(QTest::currentTestFailed())return;
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

void CalibrationReconnectTest::persistedRecordThroughPublicOpen() {
    ControllerCalibrationStore store;QString error;
    const auto persisted=store.load(binding,&error);
    QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!persisted.isEmpty());QCOMPARE(persisted,record);
    RuntimePeer peer;QVERIFY(peer.start(script).ok);
    ControllerRuntimeSession newSession(DeviceProfile::m3Reference());
    // No private candidate seam: production requestOpen must load and transport
    // this device's authenticated record in the same accepted Open request.
    QVERIFY(newSession.requestOpen(peer.port(),binding).enqueued);
    QTRY_COMPARE(stage(newSession),QString("savedHomeReady"));
    noMotion(peer);QVERIFY(!newSession.status().positionReferenced);
    QVERIFY(!newSession.status().commissioning.value("ready").toBool());
    QVERIFY(!newSession.status().motionSafetyError.isEmpty());
    for(const auto &required:{"$I","$$","$G","$#","$N"})QVERIFY2(commands(peer).contains(required),required);
    newSession.close();QTRY_VERIFY(!newSession.status().connected);
}

void CalibrationReconnectTest::connectIsReadbackOnly() {
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    connectRecord(peer,runtime,binding,record);if(QTest::currentTestFailed())return;
    QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));
    noMotion(peer);QVERIFY(!runtime.status().positionReferenced);QVERIFY(!runtime.status().motionSafetyError.isEmpty());
    for(const auto &required:{"$I","$$","$G","$#","$N"})QVERIFY2(commands(peer).contains(required),required);
    runtime.home();QTRY_VERIFY(!runtime.status().requestPending);noMotion(peer);
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}

void CalibrationReconnectTest::freshHomeAndSeparateApproach_data() {
    QTest::addColumn<bool>("matchingOrigin");
    QTest::newRow("matching-wco-needs-new-home-and-approach")<<true;
    QTest::newRow("changed-wco-needs-explicit-origin-review")<<false;
}
void CalibrationReconnectTest::freshHomeAndSeparateApproach() {
    QFETCH(bool,matchingOrigin);
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    QJsonObject configuration;
    if(matchingOrigin)configuration={{"action","set"},{"values",QJsonObject{{"offset",QJsonArray{record.value("workOriginX").toDouble(),record.value("workOriginY").toDouble()}}}}};
    connectRecord(peer,runtime,binding,record,configuration);if(QTest::currentTestFailed())return;
    QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));noMotion(peer);
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupHome));
    QTRY_COMPARE(stage(runtime),QString("savedApproachReady"));
    QCOMPARE(count(peer,"homed"),1);QCOMPARE(count(peer,"physicalMove"),0);
    QVERIFY(!runtime.status().commissioning.value("ready").toBool());
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::Jog,1,0));
    QTRY_VERIFY(!runtime.status().requestPending);QCOMPARE(count(peer,"physicalMove"),0);
    QVERIFY(data(runtime,"coordinatesObserved"));QTRY_VERIFY(!runtime.status().requestPending);
    QCOMPARE(stage(runtime),QString("savedApproachReady"));QCOMPARE(count(peer,"physicalMove"),0);
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupCorner,0));
    QTRY_COMPARE(stage(runtime),matchingOrigin?QString("complete"):QString("restoredOriginReview"));
    QCOMPARE(count(peer,"physicalMove"),1);QCOMPARE(count(peer,"coordinateUpdate"),0);QCOMPARE(count(peer,"powered"),0);
    QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({-190.,-140.}));
    if(!matchingOrigin) {
        QVERIFY(!runtime.status().commissioning.value("ready").toBool());
        const auto before=count(peer,"physicalMove");
        QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::Jog,1,0));
        QTRY_VERIFY(!runtime.status().requestPending);QCOMPARE(count(peer,"physicalMove"),before);
        QVERIFY(data(runtime,"coordinatesObserved"));QTRY_COMPARE(stage(runtime),QString("complete"));
    }
    QVERIFY(runtime.status().positionReferenced);QVERIFY(runtime.status().motionSafetyError.isEmpty());
    QCOMPARE(count(peer,"collision"),0);runtime.close();QTRY_VERIFY(!runtime.status().connected);
    QVERIFY(!runtime.status().positionReferenced);
}
void CalibrationReconnectTest::changedRecord_data() {
    QTest::addColumn<QString>("change");
    for(const auto name:{"schema","binding","profile","settings","identity","extra-field","nonfinite","outside-span","live-setting"})
        QTest::newRow(name)<<QString::fromLatin1(name);
}
void CalibrationReconnectTest::changedRecord() {
    QFETCH(QString,change);auto candidate=record;QJsonObject configuration;
    if(change=="schema")candidate["schema"]=2;
    if(change=="binding")candidate["binding"]=QString(64,'b');
    if(change=="profile")candidate["profileFingerprint"]=QString(64,'b');
    if(change=="settings")candidate["settingsFingerprint"]=QString(64,'b');
    if(change=="identity")candidate["deviceIdentityHash"]=QString(64,'b');
    if(change=="extra-field")candidate["positionReferenced"]=true;
    if(change=="nonfinite")candidate["homeX"]=std::numeric_limits<double>::quiet_NaN();
    if(change=="outside-span")candidate["maxX"]=10001.;
    if(change=="live-setting")configuration={{"action","plantConfig"},{"setting",QJsonArray{100,81.}}};
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    connectRecord(peer,runtime,binding,candidate,configuration);if(QTest::currentTestFailed())return;
    QTRY_COMPARE(stage(runtime),QString("mechanics"));
    QVERIFY(!runtime.status().commissioning.value("restoreReason").toString().isEmpty());
    noMotion(peer);QVERIFY(!runtime.status().positionReferenced);QVERIFY(!runtime.status().motionSafetyError.isEmpty());
    // A fresh native proof cannot substitute for a failed saved-record match.
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupHome));
    QTRY_VERIFY(!runtime.status().requestPending);noMotion(peer);
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
void CalibrationReconnectTest::identity_data() {
    QTest::addColumn<QString>("identity");QTest::addColumn<bool>("matches");
    QTest::newRow("same-colon")<<QString("02:11:22:33:44:55")<<true;
    QTest::newRow("same-hyphen")<<QString("02-11-22-33-44-55")<<true;
    QTest::newRow("different-controller")<<QString("02:11:22:33:44:56")<<false;
    QTest::newRow("mixed-delimiters")<<QString("02-11:22-33-44-55")<<false;
    QTest::newRow("zero")<<QString("00:00:00:00:00:00")<<false;
    QTest::newRow("broadcast")<<QString("ff:ff:ff:ff:ff:ff")<<false;
    QTest::newRow("duplicate-field")<<QString("02:11:22:33:44:55:MAC=02:11:22:33:44:55")<<false;
}
void CalibrationReconnectTest::identity() {
    QFETCH(QString,identity);QFETCH(bool,matches);
    const auto peerScript=identifiedScript(identity);QVERIFY(!peerScript.isEmpty());
    RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    connectRecord(peer,runtime,binding,record,{},peerScript);if(QTest::currentTestFailed())return;
    if(matches)QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));
    else QTRY_VERIFY(stage(runtime)=="mechanics"||stage(runtime)=="fault");
    noMotion(peer);QVERIFY(!runtime.status().positionReferenced);
    QVERIFY(!runtime.status().commissioning.value("ready").toBool());
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}
void CalibrationReconnectTest::revocation_data() {
    QTest::addColumn<QString>("loss");
    for(const auto name:{"stop","reset-banner","settings-change","disconnect"})QTest::newRow(name)<<QString::fromLatin1(name);
}
void CalibrationReconnectTest::revocation() {
    QFETCH(QString,loss);RuntimePeer peer;ControllerRuntimeSession runtime(DeviceProfile::m3Reference());
    connectRecord(peer,runtime,binding,record);if(QTest::currentTestFailed())return;
    QTRY_COMPARE(stage(runtime),QString("savedHomeReady"));
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupHome));QTRY_COMPARE(stage(runtime),QString("savedApproachReady"));
    QVERIFY(ControllerRuntimeTestAccess::native(runtime,ControllerNativeAction::SetupCorner,0));QTRY_COMPARE(stage(runtime),QString("restoredOriginReview"));
    QVERIFY(data(runtime,"coordinatesObserved"));QTRY_COMPARE(stage(runtime),QString("complete"));QVERIFY(runtime.status().positionReferenced);
    if(loss=="stop")runtime.stop();
    if(loss=="reset-banner")QVERIFY(peer.control({{"action","reply"},{"data","Grbl 1.3a ['$' for help]\r\n"}}).ok);
    if(loss=="settings-change")QVERIFY(peer.control({{"action","reply"},{"data","$100=81\r\n"}}).ok);
    if(loss=="disconnect")QVERIFY(peer.control({{"action","disconnect"}}).ok);
    QTRY_VERIFY(!runtime.status().positionReferenced);QVERIFY(stage(runtime)!="complete");
    QVERIFY(!runtime.status().motionSafetyError.isEmpty());
    if(loss!="disconnect") {
        const auto before=count(peer,"physicalMove");
        QVERIFY(data(runtime,"coordinatesObserved"));QTRY_VERIFY(!runtime.status().requestPending);
        QCOMPARE(count(peer,"physicalMove"),before);QCOMPARE(count(peer,"powered"),0);
        QVERIFY(!runtime.status().positionReferenced);
    }
    runtime.close();QTRY_VERIFY(!runtime.status().connected);
}

QTEST_MAIN(CalibrationReconnectTest)
#include "calibrationreconnect_test.moc"
