#include "accessorycontract.h"
#include <QtTest>
#include <type_traits>

class OperatorExtractionTest : public QObject {
    Q_OBJECT
    using A = AccessoryContract;
    static A::OperatorContext context(char session='a',char operation='b',A::OperatorAction action=A::OperatorAction::StartJob) {
        return {QByteArray(64,session),QByteArray(64,operation),action};
    }
    static auto witness(A &contract,const A::OperatorContext &value,qint64 now=100) {
        return contract.submitOperatorAttestation(value,true,true,now,now);
    }
private slots:
    void sensorDefaultAndOpaqueWitnessCannotBeBypassed() {
        static_assert(!std::is_default_constructible_v<A::OperatorWitness>);
        A a;
        QCOMPARE(a.supervisionMode(),A::SupervisionMode::SensorVerified);
        QVERIFY(!a.preflightError(100).isEmpty());QVERIFY(!a.begin(100));
        QVERIFY(!witness(a,context()));QVERIFY(!a.selectOperatorAttendedExternal({}));
        QVERIFY(!a.selectOperatorAttendedExternal(QByteArray(64,'X')));
        QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        QVERIFY(!a.preflightError(100).isEmpty());QVERIFY(!a.begin(100));
        QVERIFY(a.supervisionNotice().contains("nicht gemessen"));
    }
    void explicitAttestationNeverCreatesFlowAckOrOutputMapping() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto token=witness(a,context());QVERIFY(token);
        QVERIFY(a.preflightError(100).isEmpty());QVERIFY(!a.begin(100));
        QVERIFY(a.beginOperator(context(),*token,100));QVERIFY(a.activeLease());
        QCOMPARE(a.state(A::Role::EnclosureExtraction).physical,A::PhysicalState::Unknown);
        QCOMPARE(a.state(A::Role::EnclosureExtraction).command,A::CommandState::Unknown);
        QVERIFY(!a.state(A::Role::EnclosureExtraction).requestedOn);
        QCOMPARE(a.state(A::Role::EnclosureExtraction).observedAtMs,qint64(-1));
        QVERIFY(!a.verified(A::Role::EnclosureExtraction));
        QVERIFY(!a.request(A::Role::EnclosureExtraction,true));
        QVERIFY(!a.observe(A::Role::EnclosureExtraction,A::PhysicalState::Flowing,"fake",100,100));
        QVERIFY(!a.configure(A::Role::EnclosureExtraction,{"M8","guessed",true}));
        QVERIFY(!a.overridesError(100).isEmpty());
        QVERIFY(a.preflightError(100000).isEmpty()); // Human lease, never refreshed sensor data.
        QCOMPARE(a.state(A::Role::EnclosureExtraction).physical,A::PhysicalState::Unknown);
    }
    void absentOffFutureStaleAndOverflowStatementsRejectAndReplaceConsent() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        QVERIFY(!a.submitOperatorAttestation(context(),false,true,100,100));
        QVERIFY(!a.submitOperatorAttestation(context(),true,false,100,100));
        QVERIFY(!a.submitOperatorAttestation(context(),true,true,101,100));
        QVERIFY(!a.submitOperatorAttestation(context(),true,true,-1,100));
        QVERIFY(!a.submitOperatorAttestation(context(),true,true,0,30001));
        QVERIFY(!a.submitOperatorAttestation(context(),true,true,std::numeric_limits<qint64>::max(),std::numeric_limits<qint64>::max()));
        auto token=witness(a,context(),100);QVERIFY(token);
        QVERIFY(!a.submitOperatorAttestation(context(),false,true,100,100));
        QVERIFY(!a.beginOperator(context(),*token,100));
        token=witness(a,context(),101);QVERIFY(token);
        QVERIFY(!a.beginOperator(context(),*token,30102));
        token=witness(a,context(),102);QVERIFY(token);
        QVERIFY(!a.beginOperator(context(),*token,101));
    }
    void exactFreshnessBoundaryAndOneShot() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto token=witness(a,context(),100);QVERIFY(token);
        QCOMPARE(token->witnessedAtMs(),qint64(100));QCOMPARE(token->expiresAtMs(),qint64(30100));
        QVERIFY(a.beginOperator(context(),*token,30100));
        a.transition(A::Event::Complete);QVERIFY(!a.beginOperator(context(),*token,30100));
        QVERIFY(a.releaseRequired());QVERIFY(a.releaseAfterSafeClearance(true));
        auto again=witness(a,context('a','c'),30101);QVERIFY(again);
        QVERIFY(a.beginOperator(context('a','c'),*again,30101));
    }
    void noTransferAcrossSessionOperationActionInstanceOrCopy() {
        A a,b;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        QVERIFY(b.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto token=witness(a,context());QVERIFY(token);
        QVERIFY(!b.beginOperator(context(),*token,100));
        A copied=a;QVERIFY(!copied.beginOperator(context(),*token,100));
        QVERIFY(!a.beginOperator(context('c','b'),*token,100));
        token=witness(a,context(),101);QVERIFY(token);
        QVERIFY(!a.beginOperator(context('a','c'),*token,101));
        token=witness(a,context(),102);QVERIFY(token);
        QVERIFY(!a.beginOperator(context('a','b',A::OperatorAction::Focus),*token,102));
        token=witness(a,context(),103);QVERIFY(token);QVERIFY(a.beginOperator(context(),*token,103));
        A activeCopy=a;QVERIFY(!activeCopy.preflightError(103).isEmpty());
    }
    void supersessionAndSourceDiscardNeverTransferConsentOrStopAnActiveJob() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto first=witness(a,context(),100);QVERIFY(first);
        auto second=witness(a,context('a','c'),101);QVERIFY(second);
        QVERIFY(!a.operatorAdmissionError(context(),*first,101).isEmpty());
        QVERIFY(a.beginOperator(context('a','c'),*second,101));
        a.discardOperatorWitness();QVERIFY(a.activeLease());QVERIFY(a.preflightError(102).isEmpty());
        a.transition(A::Event::Stop);QVERIFY(a.releaseAfterSafeClearance(true));
        auto next=witness(a,context('a','d'),103);QVERIFY(next);a.discardOperatorWitness();
        QVERIFY(!a.beginOperator(context('a','d'),*next,103));
    }
    void pauseRequiresFreshSameOperationResume() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto start=witness(a,context());QVERIFY(start);QVERIFY(a.beginOperator(context(),*start,100));
        const auto resume=context('a','b',A::OperatorAction::Resume);
        QVERIFY(!witness(a,resume,101)); // Still running, not paused.
        a.transition(A::Event::Pause);QVERIFY(a.activeLease());QVERIFY(a.preflightError(102).isEmpty());
        QVERIFY(!a.beginOperator(resume,*start,102));
        QVERIFY(!witness(a,context('a','c',A::OperatorAction::Resume),103));
        auto token=witness(a,resume,104);QVERIFY(token);QVERIFY(a.beginOperator(resume,*token,104));
        a.transition(A::Event::Pause);QVERIFY(!a.beginOperator(resume,*token,105));
        token=witness(a,resume,106);QVERIFY(token);QVERIFY(a.beginOperator(resume,*token,106));
    }
    void focusCannotResumeAndCannotBeUnattended() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        const auto focus=context('a','f',A::OperatorAction::Focus);
        auto token=witness(a,focus);QVERIFY(token);QVERIFY(a.beginOperator(focus,*token,100));
        a.transition(A::Event::Pause);QVERIFY(!witness(a,context('a','f',A::OperatorAction::Resume),101));
        a.operatorNoLongerAttending();QVERIFY(!a.preflightError(101).isEmpty());QVERIFY(!a.activeLease());
        QVERIFY(!witness(a,context(),102));
        QVERIFY(a.releaseAfterSafeClearance(true));token=witness(a,context(),103);QVERIFY(token);
        QVERIFY(a.beginOperator(context(),*token,103));
    }
    void lifecycleRequiresNewWitnessAndNeverTurnsOutputOff_data() {
        QTest::addColumn<int>("event");
        for(auto e:{A::Event::Complete,A::Event::Stop,A::Event::Fault,A::Event::Disconnect})
            QTest::newRow(qPrintable(QString::number(int(e))))<<int(e);
    }
    void lifecycleRequiresNewWitnessAndNeverTurnsOutputOff() {
        QFETCH(int,event);A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto token=witness(a,context());QVERIFY(token);QVERIFY(a.beginOperator(context(),*token,100));
        a.transition(A::Event(event));QVERIFY(!a.activeLease());QVERIFY(a.releaseRequired());
        QVERIFY(!a.beginOperator(context(),*token,101));QVERIFY(!a.request(A::Role::EnclosureExtraction,false));
        QVERIFY(!a.selectSensorVerified());QVERIFY(a.releaseAfterSafeClearance(true));
        if(A::Event(event)==A::Event::Disconnect){
            QVERIFY(!witness(a,context(),102));QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'c')));
            token=witness(a,context('c','d'),103);QVERIFY(token);QVERIFY(a.beginOperator(context('c','d'),*token,103));
        }else{
            token=witness(a,context('a','c'),103);QVERIFY(token);QVERIFY(a.beginOperator(context('a','c'),*token,103));
        }
    }
    void manualExtractionDoesNotSubstituteForRequiredAirAssistOrSensorPolicy() {
        A a;QVERIFY(a.selectOperatorAttendedExternal(QByteArray(64,'a')));
        auto token=witness(a,context());QVERIFY(token);QVERIFY(!a.beginOperator(context(),*token,100,true));
        QVERIFY(a.selectSensorVerified());QVERIFY(!a.preflightError(100).isEmpty());QVERIFY(!a.begin(100));
        QVERIFY(!witness(a,context(),101));
    }
};
QTEST_GUILESS_MAIN(OperatorExtractionTest)
#include "operator_extraction_test.moc"
