#include "accessorycontract.h"
#include <QtTest>

using Role = AccessoryContract::Role;
using Physical = AccessoryContract::PhysicalState;
using Event = AccessoryContract::Event;
class AccessoryContractTest : public QObject {
    Q_OBJECT
    static AccessoryContract::Mapping extraction() {
        return {"mock/extractor-relay", "fixture-only verified independent extraction output",
                true};
    }
    static bool startExtraction(AccessoryContract &contract, qint64 now) {
        if (!contract.configure(Role::EnclosureExtraction, extraction()))
            return false;
        const auto request = contract.request(Role::EnclosureExtraction, true);
        return request && contract.acknowledge(*request, true) &&
               contract.observe(Role::EnclosureExtraction, Physical::Flowing,
                                "simulated flow sensor (not physical hardware)", now, now);
    }
  private slots:
    void unknownDefaultsBlockBeforeAnyRequest() {
        AccessoryContract contract;
        QVERIFY(contract.preflightError(100).contains("Gehäuseabsaugung"));
        QVERIFY(!contract.begin(100));
        QVERIFY(!contract.request(Role::EnclosureExtraction, true));
        QVERIFY(
            !contract.observe(Role::EnclosureExtraction, Physical::Flowing, "pretend", 100, 100));
        QVERIFY(!contract.configure(Role::EnclosureExtraction, {"relay", "", true}));
        QVERIFY(contract.configure(Role::EnclosureExtraction, {"relay", "unverified", false}));
        QVERIFY(!contract.request(Role::EnclosureExtraction, true));
    }
    void ackAndPhysicalFlowAreIndependent() {
        AccessoryContract contract;
        QVERIFY(contract.configure(Role::EnclosureExtraction, extraction()));
        const auto request = contract.request(Role::EnclosureExtraction, true);
        QVERIFY(request);
        QVERIFY(contract.acknowledge(*request, true));
        QVERIFY(contract.state(Role::EnclosureExtraction).physical == Physical::Unknown);
        QVERIFY(contract.preflightError(100).contains("Luftstrom"));
        QVERIFY(!contract.begin(100));
        QVERIFY(contract.observe(Role::EnclosureExtraction, Physical::NotFlowing, "flow sensor",
                                 100, 100));
        QVERIFY(!contract.begin(100));
        QVERIFY(contract.observe(Role::EnclosureExtraction, Physical::Flowing, "flow sensor", 101,
                                 101));
        QVERIFY(contract.begin(101));
        QVERIFY(contract.preflightError(3101).isEmpty());
        QVERIFY(!contract.preflightError(3102).isEmpty());
        QVERIFY(!contract.observe(Role::EnclosureExtraction, Physical::Flowing, "", 102, 102));
        QVERIFY(
            !contract.observe(Role::EnclosureExtraction, Physical::Flowing, "future", 103, 102));
        QVERIFY(!contract.observe(Role::EnclosureExtraction, Physical::Flowing, "older", 100, 102));
        QVERIFY(!contract.preflightError(100).isEmpty()); // monotonic clock regression
    }
    void separateRolesNeverAliasOrSatisfyEachOther() {
        AccessoryContract contract;
        QVERIFY(contract.configure(Role::AirAssist, {"mock/air-pump", "fixture", true}));
        const auto air = contract.request(Role::AirAssist, true);
        QVERIFY(air);
        QVERIFY(contract.acknowledge(*air, true));
        QVERIFY(contract.observe(Role::AirAssist, Physical::Flowing, "mock sensor", 100, 100));
        QVERIFY(!contract.begin(100));
        QVERIFY(!contract.configure(Role::EnclosureExtraction, {"mock/air-pump", "fixture", true}));
        QVERIFY(startExtraction(contract, 100));
        QVERIFY(contract.begin(100, true));
        const auto airOff = contract.request(Role::AirAssist, false);
        QVERIFY(airOff);
        QVERIFY(contract.acknowledge(*airOff, true));
        QVERIFY(contract.preflightError(100).isEmpty());
        QVERIFY(contract.preflightError(100, true).contains("Air Assist"));
    }
    void lifecycleNeverGeneratesExtractionOffOrReleasesOnItsOwn() {
        for (Event event :
             {Event::Pause, Event::Complete, Event::Stop, Event::Fault, Event::Disconnect}) {
            AccessoryContract contract;
            QVERIFY(startExtraction(contract, 100));
            QVERIFY(contract.begin(100));
            contract.transition(event);
            QVERIFY(contract.releaseRequired());
            QVERIFY(!contract.request(Role::EnclosureExtraction, false));
            QVERIFY(!contract.configure(Role::EnclosureExtraction, extraction()));
            QVERIFY(!contract.releaseAfterSafeClearance(false));
            if (event == Event::Pause) {
                QVERIFY(!contract.releaseAfterSafeClearance(true));
                QVERIFY(contract.preflightError(100).isEmpty());
                contract.transition(Event::Stop);
            }
            if (event == Event::Fault || event == Event::Disconnect) {
                QVERIFY(!contract.preflightError(100).isEmpty());
                QVERIFY(contract.state(Role::EnclosureExtraction).physical == Physical::Unknown);
            }
            QVERIFY(contract.releaseAfterSafeClearance(true));
            QVERIFY(contract.request(Role::EnclosureExtraction, false));
            QVERIFY(!contract.begin(100));
        }
    }
    void delayedDuplicateAndFailedAcksCannotGrantPermission() {
        AccessoryContract contract;
        QVERIFY(contract.configure(Role::EnclosureExtraction, extraction()));
        const auto first = contract.request(Role::EnclosureExtraction, true);
        const auto current = contract.request(Role::EnclosureExtraction, true);
        QVERIFY(first && current);
        QVERIFY(!contract.acknowledge(*first, true));
        QVERIFY(contract.acknowledge(*current, false));
        QVERIFY(!contract.acknowledge(*current, true));
        QVERIFY(contract.observe(Role::EnclosureExtraction, Physical::Flowing, "sensor", 100, 100));
        QVERIFY(!contract.begin(100));
        const auto pending = contract.request(Role::EnclosureExtraction, true);
        QVERIFY(pending);
        contract.transition(Event::Disconnect);
        QVERIFY(!contract.acknowledge(*pending, true));
        QVERIFY(!contract.request(Role::EnclosureExtraction, false));
    }
    void simulatorUsesExplicitEvidenceAndNoHardwareTransport() {
        AccessoryContract simulated;
        QVERIFY(startExtraction(simulated, 10));
        QVERIFY(simulated.state(Role::EnclosureExtraction).physicalEvidence.contains("simulated"));
        QVERIFY(simulated.begin(10));
        simulated.transition(Event::Complete);
        QVERIFY(simulated.releaseRequired());
        QVERIFY(!simulated.request(Role::EnclosureExtraction, false));
        QVERIFY(simulated.releaseAfterSafeClearance(true));
        const auto off = simulated.request(Role::EnclosureExtraction, false);
        QVERIFY(off);
        QVERIFY(!off->enabled);
        QVERIFY(off->outputId.startsWith("mock/"));
    }
};
QTEST_GUILESS_MAIN(AccessoryContractTest)
#include "accessorycontract_test.moc"
