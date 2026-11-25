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
template <class T>
concept PublicCommittedStartAdmission =
    requires(T &value, const typename T::OperatorContext &context) {
        value.beginOperatorWithCommittedStartIntent(context, 100, 200, 90100);
    };
static_assert(!PublicCommittedStartAdmission<AccessoryContract>);

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
    void committedStartPreservesOriginalAgeAcrossHomingWithoutFakeWitness() {
        A a;
        QVERIFY(selected(a));
        QVERIFY(!a.activeLease()); // Selection/setup alone creates no presence.
        QVERIFY(a.beginOperatorWithCommittedStartIntent(context(), 100, 350, 90100));
        QVERIFY(a.activeLease());
        QVERIFY(!a.m_operatorPending);
        QCOMPARE(a.m_lastOperatorWitnessAt, qint64(100));
        QVERIFY(a.preflightError(90101).isEmpty());
        QVERIFY(!a.releaseRequired());
        QCOMPARE(a.state(A::Role::EnclosureExtraction).physical, A::PhysicalState::Unknown);
        QCOMPARE(a.state(A::Role::EnclosureExtraction).command, A::CommandState::Unknown);
        QVERIFY(!a.verified(A::Role::EnclosureExtraction));
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context('c'), 100, 350, 90100));
        a.transition(A::Event::Complete);
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context(), 100, 350, 90101));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 90101, 90101));
        QVERIFY(a.beginOperatorWithCommittedStartIntent(context('c'), 90101, 90102, 90103));
        A ordinary;
        QVERIFY(selected(ordinary));
        QVERIFY(!ordinary.beginOperatorWithStartIntent(context(), 100, 90100));
    }
    void committedStartTimeBounds_data() {
        QTest::addColumn<qint64>("observed");
        QTest::addColumn<qint64>("admitted");
        QTest::addColumn<qint64>("now");
        QTest::addColumn<bool>("valid");
        QTest::newRow("immediate") << qint64(0) << qint64(0) << qint64(0) << true;
        QTest::newRow("last-boundary") << qint64(100) << qint64(350) << qint64(120100) << true;
        QTest::newRow("negative-observation") << qint64(-1) << qint64(0) << qint64(1) << false;
        QTest::newRow("admit-before-click") << qint64(100) << qint64(99) << qint64(200) << false;
        QTest::newRow("late-native-commit") << qint64(100) << qint64(351) << qint64(2000) << false;
        QTest::newRow("future-commit") << qint64(100) << qint64(200) << qint64(199) << false;
        QTest::newRow("operation-expired") << qint64(100) << qint64(350) << qint64(120101) << false;
        const auto max = std::numeric_limits<qint64>::max();
        QTest::newRow("large-valid-clock")
            << qint64(max - 120000) << qint64(max - 119750) << max << true;
        QTest::newRow("large-expired-clock") << qint64(0) << qint64(250) << max << false;
    }
    void committedStartTimeBounds() {
        QFETCH(qint64, observed);
        QFETCH(qint64, admitted);
        QFETCH(qint64, now);
        QFETCH(bool, valid);
        A a;
        QVERIFY(selected(a));
        QCOMPARE(a.beginOperatorWithCommittedStartIntent(context(), observed, admitted, now),
                 valid);
        QCOMPARE(a.activeLease(), valid);
        if (valid) {
            QCOMPARE(a.m_lastOperatorWitnessAt, observed);
            a.transition(A::Event::Complete);
        }
        // Neither time restamping nor switching helper can replay the operation.
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context(), 100, 100, 100));
        QVERIFY(!a.beginOperatorWithStartIntent(context(), 100, 100));
    }
    void committedStartRetainsFaultModeSessionAndIndependentAccessoryGuards() {
        for (const auto event : {A::Event::Fault, A::Event::Disconnect}) {
            A a;
            QVERIFY(selected(a));
            a.transition(event);
            QVERIFY(!a.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100));
            QVERIFY(a.faulted());
            QVERIFY(!a.activeLease());
        }
        A sensor;
        QVERIFY(!sensor.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100));
        A external;
        QVERIFY(external.selectOperatorAttendedExternal(QByteArray(64, 'a')));
        QVERIFY(!external.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100));
        A a;
        QVERIFY(selected(a));
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(
            context('b', A::OperatorAction::StartJob, 'c'), 100, 200, 90100));
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context('b', A::OperatorAction::Focus),
                                                         100, 200, 90100));
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context('b', A::OperatorAction::Resume),
                                                         100, 200, 90100));
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100, true));
        QVERIFY(!a.activeLease());
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100, false));
        QVERIFY(a.beginOperatorWithCommittedStartIntent(context('c'), 100, 200, 90100));
        a.transition(A::Event::Pause);
        QVERIFY(!a.beginOperatorWithCommittedStartIntent(context('d'), 100, 200, 90101));
        QVERIFY(a.activeLease()); // Not an implicit Resume or lease replacement.
        A release;
        QVERIFY(release.configure(A::Role::EnclosureExtraction,
                                  {"synthetic-output", "synthetic evidence", true}));
        QVERIFY(release.request(A::Role::EnclosureExtraction, true));
        QVERIFY(release.releaseRequired());
        QVERIFY(!release.beginOperatorWithCommittedStartIntent(context(), 100, 200, 90100));
        QVERIFY(release.releaseRequired());
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
