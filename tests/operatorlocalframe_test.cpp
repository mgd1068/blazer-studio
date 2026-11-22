#include "machine.h"
#include "document.h"
#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "support/runtimepeer.h"
#include "support/syntheticnativeproof.h"
#include <QtTest>
#include <QJsonArray>
using RuntimeTestSupport::RuntimePeer;
class ControllerRuntimeTestAccess {
public:
 static ControllerRuntimeSession&r(Machine&m){return *static_cast<ControllerRuntimeSession*>(m.session.get());}
 static bool native(Machine&m,const QString&action){
  return NativeInputIntentTestAccess::acceptedActivation(*m.nativeInput,action,[p=QPointer<Machine>(&m),action]{return p?p->nativeBinding(action,{},p->session->status()):QByteArray();},[&m,action](QObject*c){m.registerNativeActionControl(c,action);});
 }
};
class OperatorLocalFrameTest:public QObject {
 Q_OBJECT
 static QString phase(Machine&m){return m.operatorLocalFrame().value("phase").toString();}
 static int count(RuntimePeer&p,const QString&kind){return p.snapshot().object.value("counts").toObject().value(kind).toInt();}
private slots:
 void actualManualCaptureFrameStart(){
  RuntimePeer peer;QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);
  Document d;d.setBackgroundPlanning(false);d.newProject();d.addShape("rect");d.setProperty("x",5.);d.setProperty("y",5.);d.setProperty("width",5.);d.setProperty("height",5.);QVERIFY(d.setStartMode(Document::CurrentPosition));
  Machine m(&d);QVERIFY(m.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(m.ready());
  m.beginManualPlacement();QTRY_COMPARE(phase(m),QString("positioning"));QCOMPARE(count(peer,"physicalMove"),0);QVERIFY(!m.positionReferenced());
  QVERIFY(peer.control({{"action","plantConfig"},{"manualPosition",QJsonArray{-45.,-60.}},{"doorSubstate",1}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);QTRY_COMPARE(m.operatorLocalFrame().value("doorSubstate").toInt(),1);
  m.setManualTravel(80,40,60,50);QTRY_VERIFY(m.operatorLocalFrame().value("available").toMap().value("negativeX").toDouble()==80);
  m.attestManualPlacement(true,true,true,true);QTest::qWait(50);QCOMPARE(phase(m),QString("positioning"));
  QVERIFY(peer.control({{"action","plantConfig"},{"doorSubstate",0}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);QTRY_COMPARE(m.operatorLocalFrame().value("doorSubstate").toInt(),0);
  m.attestManualPlacement(true,true,true,true);QTRY_COMPARE(phase(m),QString("captureReady"));
  QVERIFY(ControllerRuntimeTestAccess::native(m,"localCapture"));m.captureOperatorLocalFrame();
  QTRY_VERIFY2(m.operatorLocalFrame().value("localPositionKnown").toBool(),qPrintable(m.operatorLocalFrame().value("reason").toString()));
  QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"homed"),0);QVERIFY(!m.positionReferenced());QVERIFY(!m.motionSafetyError().isEmpty());
  QTRY_COMPARE(phase(m),QString("travelReview"));QVERIFY(m.operatorLocalFrame().value("fits").toBool());
  QVERIFY(m.operatorLocalFrame().value("frameReady").toBool());QVERIFY(ControllerRuntimeTestAccess::native(m,"localFrame"));m.frameLocalJob();
  QTRY_VERIFY(count(peer,"physicalMove")>0);QTRY_COMPARE(phase(m),QString("travelReview"));QCOMPARE(count(peer,"powered"),0);
  QCOMPARE(peer.snapshot().object.value("actual").toArray(),QJsonArray({-45.,-60.}));
  m.approveLocalTravel(m.operatorLocalFrame().value("approvalId").toString());QTRY_COMPARE(phase(m),QString("ready"));
  m.confirmExtractionClearance(true);QTRY_VERIFY(m.extractionSupervision().value("reason").toString().startsWith("Sicheren Zustand"));m.selectAttendedExtraction();QTRY_COMPARE(m.extractionSupervision().value("mode").toString(),QString("operatorAttendedExternal"));
  m.attestExternalExtraction("start",true,true);QTRY_COMPARE(m.extractionSupervision().value("pendingAction").toString(),QString("start"));QTRY_VERIFY(m.operatorLocalFrame().value("startReady").toBool());
  const auto intent=RuntimePeer::monotonicNs();QVERIFY(ControllerRuntimeTestAccess::native(m,"localStart"));m.startLocalJob();
  QTRY_VERIFY2(count(peer,"powered")>0,qPrintable(m.operatorLocalFrame().value("reason").toString()));QTRY_VERIFY(!m.busy());
  bool moved=false;for(const auto &entry:peer.snapshot().object.value("events").toArray()){const auto e=entry.toObject();if(e.value("kind")=="physicalMove"&&e.value("atNs").toInteger()>=intent){QVERIFY(e.value("atNs").toInteger()-intent<10000000000LL);moved=true;}}QVERIFY(moved);
  QCOMPARE(count(peer,"collision"),0);QVERIFY(!m.positionReferenced());QCOMPARE(count(peer,"homed"),0);
  m.stop();QTRY_COMPARE(phase(m),QString("invalid"));QVERIFY(!m.operatorLocalFrame().value("localPositionKnown").toBool());
  m.useSimulator();QTRY_VERIFY(m.simulation()&&m.connectionPhase()=="closed");
 }
};
QTEST_MAIN(OperatorLocalFrameTest)
#include "operatorlocalframe_test.moc"
