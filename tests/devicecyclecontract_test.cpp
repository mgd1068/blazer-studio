#include "accessorycontract.h"
#include <QtTest>
#include <limits>
class DeviceCycleContractTest:public QObject {
 Q_OBJECT
 using A=AccessoryContract;
 static A::OperatorContext context(char operation='b',A::OperatorAction action=A::OperatorAction::StartJob){return {QByteArray(64,'a'),QByteArray(64,operation),action};}
 static bool selected(A&a){return a.selectOperatorSupervisedDeviceCycle(QByteArray(64,'a'));}
private slots:
 void explicitModePresenceOnlyNoOutputOrPhysicalEvidence(){
  A a;QCOMPARE(a.supervisionMode(),A::SupervisionMode::SensorVerified);QVERIFY(!a.begin(100));
  QVERIFY(!a.submitOperatorPresence(context(),true,100,100));QVERIFY(selected(a));
  QVERIFY(!a.begin(100));QVERIFY(!a.submitOperatorAttestation(context(),true,true,100,100));
  auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(a.beginOperator(context(),*w,100));
  QVERIFY(a.activeLease());QVERIFY(!a.releaseRequired());QVERIFY(a.preflightError(100000).isEmpty());
  QCOMPARE(a.state(A::Role::EnclosureExtraction).physical,A::PhysicalState::Unknown);
  QCOMPARE(a.state(A::Role::EnclosureExtraction).command,A::CommandState::Unknown);
  QVERIFY(!a.state(A::Role::EnclosureExtraction).requestedOn);QVERIFY(!a.verified(A::Role::EnclosureExtraction));
  QVERIFY(!a.request(A::Role::EnclosureExtraction,true));QVERIFY(!a.observe(A::Role::EnclosureExtraction,A::PhysicalState::Flowing,"fake",100,100));
  QVERIFY(!a.configure(A::Role::EnclosureExtraction,{"M8","guess",true}));QVERIFY(!a.overridesError(100).isEmpty());
  QVERIFY(a.supervisionNotice().contains("unbekannt"));QVERIFY(a.supervisionNotice().contains("nicht rücklesbar"));
 }
 void falseFutureStaleOverflowAndReplacementCannotGrant(){
  A a;QVERIFY(selected(a));QVERIFY(!a.submitOperatorPresence(context(),false,100,100));
  QVERIFY(!a.submitOperatorPresence(context(),true,101,100));QVERIFY(!a.submitOperatorPresence(context(),true,-1,100));
  QVERIFY(!a.submitOperatorPresence(context(),true,0,30001));
  QVERIFY(!a.submitOperatorPresence(context(),true,std::numeric_limits<qint64>::max(),std::numeric_limits<qint64>::max()));
  auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(!a.submitOperatorPresence(context(),false,100,100));
  QVERIFY(!a.beginOperator(context(),*w,100));
  w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(!a.beginOperator(context(),*w,30101));QVERIFY(!a.beginOperator(context(),*w,100));
  w=a.submitOperatorPresence(context(),true,30101,30101);QVERIFY(w);QVERIFY(a.beginOperator(context(),*w,60101));
 }
 void sourceSessionActionAndIssuerAreNotTransferable(){
  A a,b;QVERIFY(selected(a));QVERIFY(selected(b));auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);
  auto other=context();other.sessionBinding=QByteArray(64,'c');QVERIFY(!b.beginOperator(context(),*w,100));
  QVERIFY(!a.operatorAdmissionError(other,*w,100).isEmpty());
  QVERIFY(!a.operatorAdmissionError(context('c'),*w,100).isEmpty());
  QVERIFY(!a.operatorAdmissionError(context('b',A::OperatorAction::Focus),*w,100).isEmpty());
  QVERIFY(a.beginOperator(context(),*w,100));QVERIFY(!a.beginOperator(context(),*w,100));
 }
 void pauseResumeNeedsNewSameJobPresence(){
  A a;QVERIFY(selected(a));auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(a.beginOperator(context(),*w,100));
  a.transition(A::Event::Pause);QVERIFY(a.activeLease());QVERIFY(!a.beginOperator(context(),*w,100));
  QVERIFY(!a.submitOperatorPresence(context('c',A::OperatorAction::Resume),true,101,101));
  const auto resume=context('b',A::OperatorAction::Resume);w=a.submitOperatorPresence(resume,true,102,102);QVERIFY(w);
  QVERIFY(a.beginOperator(resume,*w,102));a.transition(A::Event::Pause);QVERIFY(!a.beginOperator(resume,*w,103));
  w=a.submitOperatorPresence(resume,true,103,103);QVERIFY(w);QVERIFY(a.beginOperator(resume,*w,103));
 }
 void lifecycle_data(){QTest::addColumn<int>("event");for(auto e:{A::Event::Stop,A::Event::Complete,A::Event::Fault,A::Event::Disconnect})QTest::newRow(qPrintable(QString::number(int(e))))<<int(e);}
 void lifecycle(){QFETCH(int,event);A a;QVERIFY(selected(a));auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(a.beginOperator(context(),*w,100));
  a.transition(A::Event(event));QVERIFY(!a.activeLease());QVERIFY(!a.beginOperator(context(),*w,101));
  QCOMPARE(a.state(A::Role::EnclosureExtraction).command,A::CommandState::Unknown);
  if(event==int(A::Event::Fault)||event==int(A::Event::Disconnect)){QVERIFY(a.faulted());QVERIFY(!a.submitOperatorPresence(context(),true,101,101));QVERIFY(a.releaseAfterSafeClearance(true));}
  if(event==int(A::Event::Disconnect))QVERIFY(!a.submitOperatorPresence(context(),true,102,102));
  else QVERIFY(a.submitOperatorPresence(context('c'),true,102,102));
 }
 void withdrawalFaultsAndFocusCannotResume(){
  A a;QVERIFY(selected(a));const auto focus=context('b',A::OperatorAction::Focus);auto w=a.submitOperatorPresence(focus,true,100,100);QVERIFY(w);QVERIFY(a.beginOperator(focus,*w,100));
  a.transition(A::Event::Pause);QVERIFY(!a.submitOperatorPresence(context('b',A::OperatorAction::Resume),true,101,101));
  a.operatorNoLongerAttending();QVERIFY(a.faulted());QVERIFY(!a.activeLease());QVERIFY(!a.preflightError(101).isEmpty());
 }
 void oldExternalModeStillRequiresActualExplicitExternalDeclaration(){
  A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
  QVERIFY(!a.submitOperatorPresence(context(),true,100,100));QVERIFY(!a.submitOperatorAttestation(context(),false,true,100,100));
  auto w=a.submitOperatorAttestation(context(),true,true,100,100);QVERIFY(w);QVERIFY(a.beginOperator(context(),*w,100));
  QVERIFY(a.releaseRequired());a.transition(A::Event::Complete);QVERIFY(!selected(a));QVERIFY(a.releaseAfterSafeClearance(true));QVERIFY(selected(a));
 }
 void airAssistNotAliasedToEitherDeviceFan(){A a;QVERIFY(selected(a));auto w=a.submitOperatorPresence(context(),true,100,100);QVERIFY(w);QVERIFY(!a.operatorAdmissionError(context(),*w,100,true).isEmpty());QVERIFY(!a.beginOperator(context(),*w,100,true));QVERIFY(!a.activeLease());}
};
QTEST_GUILESS_MAIN(DeviceCycleContractTest)
#include "devicecyclecontract_test.moc"
