#include "accessorycontract.h"
#include <QtTest>
#include <limits>

// The new convenience admission is NOT a public C++/QML presence or grant API.
template <class T>
concept PublicNativeStartAdmission =
    requires(T &value, const typename T::OperatorContext &context) {
        value.beginOperatorWithStartIntent(context, 100, 100);
    };
static_assert(!PublicNativeStartAdmission<AccessoryContract>);

class OperatorStartIntentContractTest : public QObject {
    Q_OBJECT
    using A = AccessoryContract;
    static A::OperatorContext context(char operation = 'b',
                                      A::OperatorAction action = A::OperatorAction::StartJob,
                                      char session = 'a') {
        return {QByteArray(64, session), QByteArray(64, operation), action};
    }
    static bool selected(A &a, char session = 'a') {
        return a.selectOperatorSupervisedDeviceCycle(QByteArray(64, session));
    }
  private slots:
    void defaultPolicyDoesNotReplaceExplicitAdvancedModesOrGrantExecution() {
        A pristine;
        QVERIFY(pristine.singleClickDeviceCycleEligible());
        QVERIFY(!pristine.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(!pristine.begin(100)); // Eligibility is not sensor evidence or a mode change.
        A explicitSensor;
        QVERIFY(explicitSensor.selectSensorVerified());
        QVERIFY(!explicitSensor.singleClickDeviceCycleEligible());
        for (const auto role : {A::Role::EnclosureExtraction, A::Role::AirAssist}) {
            A configured;
            QVERIFY(configured.configure(role, {"synthetic-output", QString(), false}));
            QVERIFY(!configured.singleClickDeviceCycleEligible());
        }
        A external;
        QVERIFY(external.selectOperatorAttendedExternal(QByteArray(64, 'a')));
        QVERIFY(!external.singleClickDeviceCycleEligible());
        A device;
        QVERIFY(selected(device));
        QVERIFY(device.singleClickDeviceCycleEligible());
        device.transition(A::Event::Fault);
        QVERIFY(device.singleClickDeviceCycleEligible()); // Still only a mode policy.
        QVERIFY(!device.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(device.faulted());
    }
    void selectingFreshModeDoesNotCreatePresenceOutputOrLease() {
        A a;
        QVERIFY(selected(a));
        QVERIFY(!a.activeLease());
        QVERIFY(!a.m_operatorPending);
        QVERIFY(!a.preflightError(100).isEmpty());
        QVERIFY(!a.begin(100));
        QCOMPARE(a.state(A::Role::EnclosureExtraction).physical, A::PhysicalState::Unknown);
        QCOMPARE(a.state(A::Role::EnclosureExtraction).command, A::CommandState::Unknown);
        QVERIFY(!a.state(A::Role::EnclosureExtraction).requestedOn);
    }
    void validatedWorkerIntentUsesOriginalTimeAndConsumesWitnessImmediately() {
        A a;
        QVERIFY(selected(a));
        QVERIFY(a.beginOperatorWithStartIntent(context(), 100, 200));
        QVERIFY(a.activeLease());
        QVERIFY(!a.m_operatorPending);
        QCOMPARE(a.m_lastOperatorWitnessAt, qint64(100)); // Never refreshed to dispatch time200.
        QVERIFY(a.preflightError(201).isEmpty());
        QVERIFY(!a.releaseRequired());
        QCOMPARE(a.state(A::Role::EnclosureExtraction).physical, A::PhysicalState::Unknown);
        QCOMPARE(a.state(A::Role::EnclosureExtraction).command, A::CommandState::Unknown);
        QVERIFY(!a.verified(A::Role::EnclosureExtraction));
        QVERIFY(!a.request(A::Role::EnclosureExtraction, true));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 201));
        a.transition(A::Event::Complete);
        QVERIFY(!a.activeLease());
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 202));
        QVERIFY(a.beginOperatorWithStartIntent(context('c'), 202, 202));
    }
    void invalidOrStaleOriginalTimeCannotBeRetriedAsFresh_data() {
        QTest::addColumn<qint64>("observed");
        QTest::addColumn<qint64>("now");
        QTest::newRow("future") << qint64(101) << qint64(100);
        QTest::newRow("negative") << qint64(-1) << qint64(100);
        QTest::newRow("expired") << qint64(100) << qint64(30101);
        QTest::newRow("overflow") << std::numeric_limits<qint64>::max()
                                  << std::numeric_limits<qint64>::max();
    }
    void invalidOrStaleOriginalTimeCannotBeRetriedAsFresh() {
        QFETCH(qint64, observed);
        QFETCH(qint64, now);
        A a;
        QVERIFY(selected(a));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), observed, now));
        QVERIFY(!a.activeLease());
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100)); // Same consumed attempt.
        QVERIFY(a.beginOperatorWithStartIntent(context('c'), 100, 100));
    }
    void wrongModeSessionActionAndUnknownBindingDoNotBegin() {
        A a;
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(selected(a));
        QVERIFY(!a.beginOperatorWithStartIntent(context('b', A::OperatorAction::StartJob, 'c'), 100,
                                                100));
        QVERIFY(!a.beginOperatorWithStartIntent(context('b', A::OperatorAction::Focus), 100, 100));
        QVERIFY(!a.beginOperatorWithStartIntent(context('b', A::OperatorAction::Resume), 100, 100));
        auto bad = context();
        bad.operationBinding.clear();
        QVERIFY(!a.beginOperatorWithStartIntent(bad, 100, 100));
        QVERIFY(a.beginOperatorWithStartIntent(context(), 100, 100));
    }
    void faultAndDisconnectAreNeverClearedByTheStartAdapter() {
        A a;
        QVERIFY(selected(a));
        a.transition(A::Event::Fault);
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(a.faulted());
        QVERIFY(!selected(a));
        QVERIFY(!a.activeLease());
        QVERIFY(
            a.releaseAfterSafeClearance(true)); // Explicit synthetic recovery, not helper behavior.
        QVERIFY(a.beginOperatorWithStartIntent(context('c'), 100, 100));
        a.transition(A::Event::Disconnect);
        QVERIFY(a.faulted());
        QVERIFY(!a.beginOperatorWithStartIntent(context('d'), 101, 101));
        QVERIFY(a.faulted());
        QVERIFY(a.releaseAfterSafeClearance(true));
        QVERIFY(!a.beginOperatorWithStartIntent(context('d'), 101, 101));
        QVERIFY(selected(a, 'e'));
        QVERIFY(a.beginOperatorWithStartIntent(context('f', A::OperatorAction::StartJob, 'e'), 102,
                                               102));
    }
    void externalReleaseAndExternalModeCannotBeBypassed() {
        A a;
        QVERIFY(a.configure(A::Role::EnclosureExtraction,
                            {"synthetic-output", "synthetic evidence", true}));
        QVERIFY(a.request(A::Role::EnclosureExtraction, true));
        QVERIFY(a.releaseRequired());
        QVERIFY(!selected(a));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(a.releaseRequired());
        A external;
        QVERIFY(external.selectOperatorAttendedExternal(QByteArray(64, 'a')));
        QVERIFY(!external.beginOperatorWithStartIntent(context(), 100, 100));
        QVERIFY(!external.activeLease());
        QVERIFY(!external.submitOperatorAttestation(context(), false, true, 100, 100));
    }
    void pausedJobStillNeedsItsSeparateResumeAndKeepsThatWitness() {
        A a;
        QVERIFY(selected(a));
        QVERIFY(a.beginOperatorWithStartIntent(context(), 100, 100));
        a.transition(A::Event::Pause);
        const auto resume = context('b', A::OperatorAction::Resume);
        const auto witness = a.submitOperatorPresence(resume, true, 101, 101);
        QVERIFY(witness);
        QVERIFY(!a.beginOperatorWithStartIntent(context('c'), 101, 101));
        QVERIFY(a.activeLease());
        QVERIFY(a.beginOperator(resume, *witness, 101));
    }
    void airAssistRemainsIndependentAndFailedIntentDoesNotBecomeReusable() {
        A a;
        QVERIFY(selected(a));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100, true));
        QVERIFY(!a.activeLease());
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100, false));
        QVERIFY(a.beginOperatorWithStartIntent(context('c'), 101, 101, false));
    }
};
QTEST_GUILESS_MAIN(OperatorStartIntentContractTest)
#include "operatorstartintentcontract_test.moc"
