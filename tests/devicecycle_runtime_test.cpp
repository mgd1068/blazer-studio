#include "machine.h"
#include "document.h"
#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "support/runtimepeer.h"
#include "support/syntheticnativeproof.h"
#include <QtTest>
#include <QJsonArray>
#include <QSemaphore>
using RuntimeTestSupport::RuntimePeer;
class ControllerRuntimeTestAccess {
public:
 static ControllerRuntimeSession&r(Machine&m){return *static_cast<ControllerRuntimeSession*>(m.session.get());}
 static bool native(Machine&m,const QString&action){return NativeInputIntentTestAccess::acceptedActivation(*m.nativeInput,action,[p=QPointer<Machine>(&m),action]{return p?p->nativeBinding(action,{},p->session->status()):QByteArray();},[&m,action](QObject*c){m.registerNativeActionControl(c,action);});}
 static QVariantMap accessory(Machine&m){auto result=std::make_shared<QVariantMap>();auto done=std::make_shared<QSemaphore>();auto w=r(m).m_worker;
  if(!QMetaObject::invokeMethod(w,[w,result,done]{const auto&s=w->m_accessory.state(AccessoryContract::Role::EnclosureExtraction);*result={{"physical",int(s.physical)},{"command",int(s.command)},{"requestedOn",s.requestedOn},{"observedAt",s.observedAtMs}};done->release();},Qt::QueuedConnection)||!done->tryAcquire(1,3000))return {};return *result;}
};
class DeviceCycleRuntimeTest:public QObject {
 Q_OBJECT
 static int count(RuntimePeer&p,const QString&kind){return p.snapshot().object.value("counts").toObject().value(kind).toInt();}
 static QString phase(Machine&m){return m.operatorLocalFrame().value("phase").toString();}
 static void prepare(RuntimePeer&peer,Document&d,Machine&m){
  d.setBackgroundPlanning(false);d.newProject();d.addShape("rect");d.setProperty("x",5.);d.setProperty("y",5.);d.setProperty("width",5.);d.setProperty("height",5.);QVERIFY(d.setStartMode(Document::CurrentPosition));
  QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);
  QVERIFY(m.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(m.ready());m.beginManualPlacement();QTRY_COMPARE(phase(m),QString("positioning"));
  QVERIFY(peer.control({{"action","plantConfig"},{"manualPosition",QJsonArray{-45.,-60.}}}).ok);
  m.setManualTravel(80,40,60,50);QTRY_COMPARE(m.operatorLocalFrame().value("available").toMap().value("negativeX").toDouble(),80.);
  m.attestManualPlacement(true,true,true,true);QTRY_COMPARE(phase(m),QString("captureReady"));QVERIFY(ControllerRuntimeTestAccess::native(m,"localCapture"));m.captureOperatorLocalFrame();QTRY_COMPARE(phase(m),QString("travelReview"));
  m.approveLocalTravel(m.operatorLocalFrame().value("approvalId").toString());QTRY_COMPARE(phase(m),QString("ready"));
  m.confirmExtractionClearance(true);QTRY_VERIFY(m.extractionSupervision().value("reason").toString().contains("geklärt"));
  m.selectSupervisedDeviceCycle();QTRY_COMPARE(m.extractionSupervision().value("mode").toString(),QString("operatorSupervisedDeviceCycle"));
 }
 static void unknownFans(Machine&m){const auto state=m.extractionSupervision();QVERIFY(state.value("presenceOnly").toBool());QVERIFY(!state.value("requiresExternalExtraction").toBool());QVERIFY(!state.value("airflowMeasured").toBool());
  const auto fans=state.value("deviceFans").toList();QCOMPARE(fans.size(),2);QCOMPARE(fans[0].toMap().value("role").toString(),QString("enclosureExhaust"));QCOMPARE(fans[1].toMap().value("role").toString(),QString("laserModuleCooling"));
  for(const auto&v:fans){const auto f=v.toMap();QCOMPARE(f.value("state").toString(),QString("unknown"));QCOMPARE(f.value("feedback").toString(),QString("notReadBack"));QVERIFY(!f.value("independentControl").toBool());QVERIFY(!f.contains("running"));}
  const auto a=ControllerRuntimeTestAccess::accessory(m);QVERIFY(!a.isEmpty());QCOMPARE(a.value("physical").toInt(),int(AccessoryContract::PhysicalState::Unknown));QCOMPARE(a.value("command").toInt(),int(AccessoryContract::CommandState::Unknown));QVERIFY(!a.value("requestedOn").toBool());QCOMPARE(a.value("observedAt").toLongLong(),qint64(-1));
 }
 struct Cleanup {Machine&m;~Cleanup(){m.useSimulator();QElapsedTimer t;t.start();while((!m.simulation()||m.connectionPhase()!="closed")&&t.elapsed()<2500){QCoreApplication::processEvents();QThread::msleep(1);}}};
private slots:
 void actualMachinePresenceThenNativeStartWithoutExternalDeclaration(){RuntimePeer peer;Document d;Machine m(&d);Cleanup cleanup{m};prepare(peer,d,m);if(QTest::currentTestFailed())return;unknownFans(m);
  QVERIFY(!m.operatorLocalFrame().value("startReady").toBool());m.startLocalJob();QTest::qWait(80);QCOMPARE(count(peer,"powered"),0);
  m.attestDeviceCycle("start",false);QTest::qWait(50);QVERIFY(m.extractionSupervision().value("pendingAction").toString().isEmpty());
  m.attestExternalExtraction("start",true,true);QTest::qWait(50);QVERIFY(m.extractionSupervision().value("pendingAction").toString().isEmpty());
  m.attestDeviceCycle("start",true);QTRY_COMPARE(m.extractionSupervision().value("pendingAction").toString(),QString("start"));QTRY_VERIFY(m.operatorLocalFrame().value("startReady").toBool());QCOMPARE(count(peer,"powered"),0);unknownFans(m);
  QVERIFY(ControllerRuntimeTestAccess::native(m,"localStart"));m.startLocalJob();QTRY_VERIFY(count(peer,"powered")>0);QTRY_VERIFY(!m.busy());QTRY_VERIFY(!m.extractionSupervision().value("activeLease").toBool());unknownFans(m);
  QCOMPARE(count(peer,"collision"),0);QVERIFY(!m.positionReferenced());QCOMPARE(count(peer,"homed"),0);
 }
 void actualPauseResumeNeedsFreshPresenceAndWithdrawStops(){RuntimePeer peer;Document d;Machine m(&d);Cleanup cleanup{m};prepare(peer,d,m);if(QTest::currentTestFailed())return;
  m.attestDeviceCycle("start",true);QTRY_VERIFY(m.operatorLocalFrame().value("startReady").toBool());QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);
  QVERIFY(ControllerRuntimeTestAccess::native(m,"localStart"));m.startLocalJob();QTRY_VERIFY(m.busy());QTRY_VERIFY(m.extractionSupervision().value("activeLease").toBool());m.pause();QTRY_VERIFY(m.holdConfirmed());QTRY_COMPARE(phase(m),QString("paused"));
  QVERIFY(!m.operatorLocalFrame().value("resumeReady").toBool());m.attestDeviceCycle("resume",false);QTest::qWait(50);QVERIFY(!m.operatorLocalFrame().value("resumeReady").toBool());
  m.attestDeviceCycle("resume",true);QTRY_VERIFY(m.operatorLocalFrame().value("resumeReady").toBool());QVERIFY(ControllerRuntimeTestAccess::native(m,"localResume"));m.resumeLocalJob();QTRY_VERIFY(!m.paused());
  const auto resumed=[&peer]{for(const auto&e:peer.snapshot().object.value("events").toArray()){const auto o=e.toObject();if(o.value("kind")=="realtime"&&o.value("value").toInt()==int('~'))return true;}return false;};QTRY_VERIFY(resumed());
  unknownFans(m);m.revokeDeviceAttendance();QTRY_COMPARE(phase(m),QString("invalid"));QTRY_VERIFY(!m.extractionSupervision().value("activeLease").toBool());QVERIFY(!m.positionReferenced());
 }
 void focusConsentDoesNotAuthorizeLocalStartAndDoorStillInvalidates(){RuntimePeer peer;Document d;Machine m(&d);Cleanup cleanup{m};prepare(peer,d,m);if(QTest::currentTestFailed())return;
  m.attestDeviceCycle("focus",true);QTRY_COMPARE(m.extractionSupervision().value("pendingAction").toString(),QString("focus"));QVERIFY(!m.operatorLocalFrame().value("startReady").toBool());m.startLocalJob();QTest::qWait(60);QCOMPARE(count(peer,"powered"),0);
  m.attestDeviceCycle("start",true);QTRY_VERIFY(m.operatorLocalFrame().value("startReady").toBool());QVERIFY(peer.control({{"action","plantConfig"},{"doorSubstate",1}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);QTRY_COMPARE(phase(m),QString("invalid"));
  m.startLocalJob();QTest::qWait(60);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"physicalMove"),0);
 }
};
QTEST_MAIN(DeviceCycleRuntimeTest)
#include "devicecycle_runtime_test.moc"
