#include "machine.h"
#include "document.h"
#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "support/runtimepeer.h"
#include "support/syntheticnativeproof.h"
#include <QtTest>
#include <QJsonArray>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;
class ControllerRuntimeTestAccess {
public:
 static ControllerRuntimeSession&r(Machine&m){return *static_cast<ControllerRuntimeSession*>(m.session.get());}
 struct Barrier {QSemaphore entered,release;~Barrier(){release.release();}};
 static std::shared_ptr<Barrier> hold(Machine&m){auto b=std::make_shared<Barrier>();if(!QMetaObject::invokeMethod(r(m).m_worker,[b]{b->entered.release();b->release.tryAcquire(1,3000);},Qt::QueuedConnection)||!b->entered.tryAcquire(1,3000))return {};return b;}
 static bool native(Machine&m,const QString&action){
  return NativeInputIntentTestAccess::acceptedActivation(*m.nativeInput,action,[p=QPointer<Machine>(&m),action]{return p?p->nativeBinding(action,{},p->session->status()):QByteArray();},[&m,action](QObject*c){m.registerNativeActionControl(c,action);});
 }
};
class OperatorLocalFrameTest:public QObject {
 Q_OBJECT
 static QString phase(Machine&m){return m.operatorLocalFrame().value("phase").toString();}
 static int count(RuntimePeer&p,const QString&kind){return p.snapshot().object.value("counts").toObject().value(kind).toInt();}
 static void document(Document&d){d.setBackgroundPlanning(false);d.newProject();d.addShape("rect");d.setProperty("x",5.);d.setProperty("y",5.);d.setProperty("width",5.);d.setProperty("height",5.);d.setStartMode(Document::CurrentPosition);}
 static void captured(RuntimePeer&peer,Machine&m,double left=80,double right=40,double down=60,double up=50){
  QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);QVERIFY(m.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(m.ready());
  m.beginManualPlacement();QTRY_COMPARE(phase(m),QString("positioning"));
  QVERIFY(peer.control({{"action","plantConfig"},{"manualPosition",QJsonArray{-45.,-60.}}}).ok);
  m.setManualTravel(left,right,down,up);QTRY_VERIFY(m.operatorLocalFrame().value("available").toMap().value("negativeX").toDouble()==left);
  m.attestManualPlacement(true,true,true,true);QTRY_COMPARE(phase(m),QString("captureReady"));
  QVERIFY(ControllerRuntimeTestAccess::native(m,"localCapture"));m.captureOperatorLocalFrame();QTRY_COMPARE(phase(m),QString("travelReview"));QVERIFY(!m.positionReferenced());QCOMPARE(count(peer,"physicalMove"),0);
 }
 static void startReady(Machine&m){m.approveLocalTravel(m.operatorLocalFrame().value("approvalId").toString());QTRY_COMPARE(phase(m),QString("ready"));m.confirmExtractionClearance(true);QTRY_VERIFY(m.extractionSupervision().value("reason").toString().startsWith("Sicheren Zustand"));m.selectAttendedExtraction();QTRY_COMPARE(m.extractionSupervision().value("mode").toString(),QString("operatorAttendedExternal"));m.attestExternalExtraction("start",true,true);QTRY_VERIFY(m.operatorLocalFrame().value("startReady").toBool());}
 struct Cleanup {Machine&m;~Cleanup(){m.useSimulator();QElapsedTimer t;t.start();while((!m.simulation()||m.connectionPhase()!="closed")&&t.elapsed()<2500){QCoreApplication::processEvents();QThread::msleep(1);}}};
private slots:
 void actualManualCaptureFrameStart_data(){QTest::addColumn<int>("anchor");for(int i=0;i<9;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;}
 void actualManualCaptureFrameStart(){
  QFETCH(int,anchor);
  RuntimePeer peer;QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);
  Document d;d.setBackgroundPlanning(false);d.newProject();d.addShape("rect");d.setProperty("x",5.);d.setProperty("y",5.);d.setProperty("width",5.);d.setProperty("height",5.);QVERIFY(d.setStartMode(Document::CurrentPosition));QVERIFY(d.setJobOrigin(Document::JobOrigin(anchor)));
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
 void localBoundaryLoss_data(){QTest::addColumn<QString>("loss");for(const auto s:{"door","restart","settings","wco","disconnect"})QTest::newRow(s)<<QString::fromLatin1(s);}
 void localBoundaryLoss(){QFETCH(QString,loss);RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};captured(peer,m);if(QTest::currentTestFailed())return;
  const auto moves=count(peer,"physicalMove"),zeros=count(peer,"coordinateUpdate");
  if(loss=="door"){QVERIFY(peer.control({{"action","plantConfig"},{"doorSubstate",1}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);}
  if(loss=="restart")QVERIFY(peer.control({{"action","reply"},{"data","Grbl 1.3a ['$' for help]\r\n"}}).ok);
  if(loss=="settings")QVERIFY(peer.control({{"action","reply"},{"data","$1=50\r\n"}}).ok);
  if(loss=="wco"){QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"offset",QJsonArray{-70.,-50.}}}}}).ok);QVERIFY(peer.control({{"action","report"}}).ok);}
  if(loss=="disconnect")QVERIFY(peer.control({{"action","disconnect"}}).ok);
  QTRY_COMPARE(phase(m),QString("invalid"));QVERIFY(!m.operatorLocalFrame().value("localPositionKnown").toBool());QVERIFY(!m.positionReferenced());m.startLocalJob();QTest::qWait(80);
  if(loss!="disconnect"){QCOMPARE(count(peer,"physicalMove"),moves);QCOMPARE(count(peer,"coordinateUpdate"),zeros);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"homed"),0);}
 }
 void queuedFence_data(){QTest::addColumn<bool>("stop");QTest::newRow("source-edit-before-write")<<false;QTest::newRow("stop-before-write")<<true;}
 void queuedFence(){QFETCH(bool,stop);RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};captured(peer,m);if(QTest::currentTestFailed())return;startReady(m);if(QTest::currentTestFailed())return;
  const auto oldApproval=m.operatorLocalFrame().value("approvalId").toString();auto b=ControllerRuntimeTestAccess::hold(m);QVERIFY(b);auto release=qScopeGuard([b]{b->release.release();});
  QVERIFY(ControllerRuntimeTestAccess::native(m,"localStart"));m.startLocalJob();if(stop)m.stop();else d.setProperty("width",6.);b->release.release();
  if(stop)QTRY_COMPARE(phase(m),QString("invalid"));else {QTRY_COMPARE(phase(m),QString("travelReview"));QTRY_VERIFY(m.operatorLocalFrame().value("approvalId").toString()!=oldApproval);m.approveLocalTravel(oldApproval);QTest::qWait(50);QVERIFY(!m.operatorLocalFrame().value("approved").toBool());}
  QTest::qWait(100);QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"powered"),0);QCOMPARE(count(peer,"homed"),0);QVERIFY(!m.positionReferenced());
 }
 void noNativeAndBadApproval(){RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};captured(peer,m);if(QTest::currentTestFailed())return;
  m.approveLocalTravel("stale");QTest::qWait(50);QCOMPARE(phase(m),QString("travelReview"));QVERIFY(!m.operatorLocalFrame().value("approved").toBool());
  startReady(m);if(QTest::currentTestFailed())return;m.startLocalJob();QTest::qWait(120);QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"powered"),0);QVERIFY(!m.positionReferenced());
 }
 void holdResume_data(){QTest::addColumn<bool>("frame");QTest::newRow("job-with-new-witness")<<false;QTest::newRow("frozen-s0-frame-without-outputlease")<<true;}
 void holdResume(){QFETCH(bool,frame);RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};captured(peer,m);if(QTest::currentTestFailed())return;
  if(!frame){startReady(m);if(QTest::currentTestFailed())return;}
  QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);
  QVERIFY(ControllerRuntimeTestAccess::native(m,frame?"localFrame":"localStart"));if(frame)m.frameLocalJob();else m.startLocalJob();QTRY_VERIFY(m.busy());m.pause();QTRY_VERIFY(m.holdConfirmed());QTRY_COMPARE(phase(m),QString("paused"));
  if(!frame){QVERIFY(!m.operatorLocalFrame().value("resumeReady").toBool());m.attestExternalExtraction("resume",true,true);}
  QTRY_VERIFY(m.operatorLocalFrame().value("resumeReady").toBool());QVERIFY(ControllerRuntimeTestAccess::native(m,"localResume"));m.resumeLocalJob();QTRY_VERIFY(!m.paused());
  // The host clears its hold request before the independent PTY peer necessarily
  // receives and journals the resume byte. Wait for that wire-side evidence.
  const auto resumed=[&peer]{for(const auto&e:peer.snapshot().object.value("events").toArray()){const auto o=e.toObject();if(o.value("kind")=="realtime"&&o.value("value").toInt()==int('~'))return true;}return false;};
  QTRY_VERIFY(resumed());QVERIFY(!m.positionReferenced());if(frame)QCOMPARE(count(peer,"powered"),0);
  m.stop();QTRY_COMPARE(phase(m),QString("invalid"));QVERIFY(!m.operatorLocalFrame().value("localPositionKnown").toBool());
 }
 void measuredButTooSmallRoom(){RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};captured(peer,m,2.1,2.1,2.1,2.1);if(QTest::currentTestFailed())return;QVERIFY(m.operatorLocalFrame().value("localPositionKnown").toBool());QVERIFY(!m.operatorLocalFrame().value("fits").toBool());QVERIFY(!m.operatorLocalFrame().value("startReady").toBool());QVERIFY(!m.operatorLocalFrame().value("frameReady").toBool());m.startLocalJob();m.frameLocalJob();QTest::qWait(80);QCOMPARE(count(peer,"physicalMove"),0);QCOMPARE(count(peer,"powered"),0);QVERIFY(!m.positionReferenced());}
 void invalidRestways_data(){QTest::addColumn<double>("left");QTest::addColumn<double>("right");QTest::newRow("zero")<<0.<<40.;QTest::newRow("reserve-not-available")<<1.999<<40.;QTest::newRow("opposite-sum-overflow")<<180.<<40.;QTest::newRow("nan")<<std::numeric_limits<double>::quiet_NaN()<<40.;}
 void invalidRestways(){QFETCH(double,left);QFETCH(double,right);RuntimePeer peer;Document d;document(d);Machine m(&d);Cleanup cleanup{m};QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);QVERIFY(m.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(m.ready());m.beginManualPlacement();QTRY_COMPARE(phase(m),QString("positioning"));const auto before=m.operatorLocalFrame().value("resultRevision").toULongLong();m.setManualTravel(left,right,60,50);QTRY_VERIFY(m.operatorLocalFrame().value("resultRevision").toULongLong()>before);QVERIFY(!m.operatorLocalFrame().value("lastRequestAccepted").toBool());m.attestManualPlacement(true,true,true,true);QTest::qWait(50);QCOMPARE(phase(m),QString("positioning"));QVERIFY(!ControllerRuntimeTestAccess::native(m,"localCapture"));QCOMPARE(count(peer,"coordinateUpdate"),0);QCOMPARE(count(peer,"physicalMove"),0);}

};
QTEST_MAIN(OperatorLocalFrameTest)
#include "operatorlocalframe_test.moc"
