#include "manualtravelcheck.h"
#include <QJsonDocument>
#include <QtTest>
#include <cmath>
#include <limits>
using namespace ManualTravelCheck;
namespace {
JobPlan path(QPointF head, std::initializer_list<QPointF> points) {
    JobPlan plan;
    QPointF previous = head;
    for (const auto p : points) {
        plan.moves.append({previous, p, 600, 0, 1, true});
        previous = p;
    }
    return plan;
}
Request request(const JobPlan &plan, QPointF head, Distances available = {100, 100, 75, 75},
                double margin = 0) {
    return {head, available, margin, sourceFingerprint(plan)};
}
void equal(Distances actual, Distances expected) {
    QCOMPARE(actual.negativeX, expected.negativeX);
    QCOMPARE(actual.positiveX, expected.positiveX);
    QCOMPARE(actual.negativeY, expected.negativeY);
    QCOMPARE(actual.positiveY, expected.positiveY);
}
} // namespace
class ManualTravelCheckTest : public QObject {
    Q_OBJECT
  private slots:
    void allNineAnchors_data() {
        QTest::addColumn<int>("anchor");
        for (int n = 0; n < 9; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void allNineAnchors() {
        QFETCH(int, anchor);
        // A literal 40x20 rectangle, independently placed around each head anchor.
        const QPointF head(100, 75);
        const double left = -(anchor % 3) * 20., bottom = -(anchor / 3) * 10.;
        const QPointF a = head + QPointF(left, bottom), b = a + QPointF(40, 0),
                      c = a + QPointF(40, 20), d = a + QPointF(0, 20);
        auto plan = path(head, {a, b, c, d, a});
        auto r = request(plan, head, {50, 50, 30, 30}, 2);
        const auto result = check(plan, r);
        QVERIFY2(result.valid(), qPrintable(result.error()));
        equal(result.required(), {-left, 40 + left, -bottom, 20 + bottom});
        equal(result.available(), {50, 50, 30, 30});
        equal(result.usableAfterMargin(), {48, 48, 28, 28});
        QVERIFY(result.matches(plan, r));
        QCOMPARE(result.sourceFingerprint(), r.expectedSourceFingerprint);
        QCOMPARE(result.requestFingerprint().size(), 32);
    }
    void virtualCoordinatesAreNotPhysicalProfileCoordinates() {
        auto plan = path({-700, 400}, {{-680, 390}, {-690, 420}});
        const auto result = check(plan, request(plan, {-700, 400}, {30, 30, 30, 30}, 1));
        QVERIFY2(result.valid(), qPrintable(result.error()));
        equal(result.required(), {0, 20, 10, 20});
        QCOMPARE(plan.moves.first().from, QPointF(-700, 400));
    }
    void allMovesIncludingApproachOverscanAndReturnCount() {
        // Artwork would be10..30; first approach and overscan extend beyond it.
        const auto plan = path({10, 10}, {{0, 10}, {40, 10}, {30, 20}, {-5, 10}});
        auto r = request(plan, {10, 10}, {15, 30, 0, 10});
        const auto result = check(plan, r);
        QVERIFY(result.valid());
        equal(result.required(), {15, 30, 0, 10});
        r.available->negativeX = 14.999;
        const auto insufficientReturn = check(plan, r);
        QVERIFY(!insufficientReturn.valid());
        QVERIFY(insufficientReturn.error().contains("−X"));
        equal(insufficientReturn.required(), {15, 30, 0, 10});
        r.available = Distances{15, 29.999, 0, 10};
        const auto insufficientOverscan = check(plan, r);
        QVERIFY(!insufficientOverscan.valid());
        QVERIFY(insufficientOverscan.error().contains("+X"));
    }
    void firstFromMustMatchExplicitAnchorWithoutFuzzyOrNominalFiltering() {
        const auto plan = path({0, 0}, {{10, 20}, {30, 20}});
        auto r = request(plan, {10, 20});
        QVERIFY(!check(plan, r).valid()); // Do not silently remove nominal0-to-start approach.
        r.headAnchor = QPointF(0, 0);
        const auto result = check(plan, r);
        QVERIFY(result.valid());
        equal(result.required(), {0, 30, 0, 20});
        r.headAnchor = QPointF(1e-12, 0);
        QVERIFY(!check(plan, r).valid());
    }
    void safetyReserveAppliesEvenToUnmovedAxes() {
        const auto plan = path({0, 0}, {{10, 0}});
        auto r = request(plan, {0, 0}, {2, 12, 2, 2}, 2);
        QVERIFY(check(plan, r).valid()); // Inclusive equality at explicit reserve boundary.
        r.available->negativeY = 1.999;
        const auto result = check(plan, r);
        QVERIFY(!result.valid());
        QVERIFY(result.error().contains("−Y"));
        QVERIFY(result.usableAfterMargin().negativeY < 0);
        r.available->negativeY = 2;
        r.available->positiveX = 11.999;
        QVERIFY(!check(plan, r).valid());
    }
    void rawAndLocalAbsoluteWireEndpointsAreBothChecked_data() {
        QTest::addColumn<double>("endpoint");
        QTest::addColumn<double>("expectedRequired");
        QTest::newRow("round-out-positive") << .0046 << .005;
        QTest::newRow("round-out-negative") << -.0046 << .005;
        QTest::newRow("raw-round-in-positive") << .0044 << .0044;
        QTest::newRow("raw-round-in-negative") << -.0044 << .0044;
        QTest::newRow("tiny-raw-is-not-discarded") << .0004 << .0004;
    }
    void rawAndLocalAbsoluteWireEndpointsAreBothChecked() {
        QFETCH(double, endpoint);
        QFETCH(double, expectedRequired);
        const auto plan = path({0, 0}, {{endpoint, 0}});
        auto r = request(plan, {0, 0}, {1, 1, 1, 1});
        const auto result = check(plan, r);
        QVERIFY(result.valid());
        QCOMPARE(endpoint < 0 ? result.required().negativeX : result.required().positiveX,
                 expectedRequired);
        if (endpoint < 0)
            r.available->negativeX = expectedRequired - .00001;
        else
            r.available->positiveX = expectedRequired - .00001;
        QVERIFY(!check(plan, r).valid());
    }
    void localAbsQuantizationNeverAccumulatesRelativeDeltas() {
        JobPlan plan;
        QPointF previous;
        for (int n = 1; n <= 10; ++n) {
            const QPointF p(n * .00049, 0);
            plan.moves.append({previous, p, 600, 0, 1, true});
            previous = p;
        }
        const auto result = check(plan, request(plan, {0, 0}, {0, .005, 0, 0}));
        QVERIFY(result.valid());
        QCOMPARE(result.required().positiveX, .005); // 10 rounded G91 deltas would wrongly yield0.
    }
    void missingNonFiniteNegativeAndUnplausibleInputsFailClosed() {
        const auto plan = path({0, 0}, {{1, 1}});
        auto r = request(plan, {0, 0});
        r.headAnchor.reset();
        QVERIFY(!check(plan, r).valid());
        r = request(plan, {0, 0});
        r.available.reset();
        QVERIFY(!check(plan, r).valid());
        for (double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -1., 2000.001}) {
            r = request(plan, {0, 0});
            r.available->positiveX = value;
            QVERIFY(!check(plan, r).valid());
            r = request(plan, {0, 0});
            r.safetyMarginMm = value;
            QVERIFY(!check(plan, r).valid());
            r = request(plan, {0, 0});
            r.headAnchor = QPointF(value, 0);
            QVERIFY(!check(plan, r).valid());
        }
        r = request(plan, {0, 0});
        r.safetyMarginMm = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!check(plan, r).valid());
        r = request(plan, {0, 0});
        r.expectedSourceFingerprint.clear();
        QVERIFY(!check(plan, r).valid());
        r = request(plan, {0, 0}, {100, 100.001, 75, 75});
        QVERIFY(check(plan, r).error().contains("Profilachsweg"));
        r = request(plan, {0, 0}, {100, 100, 75.001, 75});
        QVERIFY(check(plan, r).error().contains("Profilachsweg"));
    }
    void fingerprintsBindMovesProfileAndEveryRequestValue() {
        auto plan = path({10, 20}, {{20, 20}, {20, 30}});
        auto r = request(plan, {10, 20});
        const auto result = check(plan, r);
        QVERIFY(result.valid());
        auto changed = plan;
        auto move = changed.moves[1];
        move.speed = 601;
        changed.moves.replace(1, move);
        QVERIFY(!result.matches(changed, r));
        QVERIFY(!check(changed, r).valid());
        auto changedRequest = r;
        for (int axis = 0; axis < 4; ++axis) {
            changedRequest = r;
            switch (axis) {
            case 0:
                changedRequest.available->negativeX -= 1;
                break;
            case 1:
                changedRequest.available->positiveX -= 1;
                break;
            case 2:
                changedRequest.available->negativeY -= 1;
                break;
            case 3:
                changedRequest.available->positiveY -= 1;
                break;
            }
            QVERIFY(!result.matches(plan, changedRequest));
        }
        changedRequest = r;
        changedRequest.safetyMarginMm = .1;
        QVERIFY(!result.matches(plan, changedRequest));
        changedRequest = r;
        changedRequest.headAnchor = QPointF(10.001, 20);
        QVERIFY(!result.matches(plan, changedRequest));
        auto profileJson = plan.deviceProfile().toJson();
        auto workspace = profileJson["workspace"].toObject();
        workspace["width"] = 201.;
        profileJson["workspace"] = workspace;
        QString error;
        auto profile = DeviceProfile::parse(QJsonDocument(profileJson).toJson(), error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        JobPlan differentProfile(*profile);
        differentProfile.moves = plan.moves;
        QVERIFY(!result.matches(differentProfile, r));
        QVERIFY(!check(differentProfile, r).valid());
        QVERIFY(result.matches(plan, r));
    }
    void malformedAndOverBudgetPlansAreRejected() {
        const auto good = path({0, 0}, {{1, 1}, {2, 1}});
        const auto r = request(good, {0, 0});
        QVERIFY(!check(JobPlan{}, r).valid());
        for (int variant = 0; variant < 9; ++variant) {
            auto plan = good;
            auto move = plan.moves[1];
            switch (variant) {
            case 0:
                move.from.setX(1 + 1e-12);
                break;
            case 1:
                move.to.setY(std::numeric_limits<double>::quiet_NaN());
                break;
            case 2:
                move.power = 1;
                break; // Powered rapid forbidden.
            case 3:
                move.speed = 0;
                break;
            case 4:
                move.layer = 64;
                break;
            case 5:
                move.slice = -1;
                break;
            case 6:
                move.to.setX(2001);
                break;
            case 7:
                plan.cancelled = true;
                break;
            case 8:
                plan.errors = {"invalid source"};
                break;
            }
            plan.moves.replace(1, move);
            QVERIFY(sourceFingerprint(plan).isEmpty());
            QVERIFY(!check(plan, r).valid());
        }
        auto tooMany = good;
        tooMany.moves.resize(JobPlan::maximumMoves + 1);
        QVERIFY(sourceFingerprint(tooMany).isEmpty());
        QVERIFY(!check(tooMany, r).valid());
        const auto cancelled = std::make_shared<std::atomic_bool>(true);
        QVERIFY(sourceFingerprint(good, cancelled).isEmpty());
        QVERIFY(!check(good, r, cancelled).valid());
        const auto result = check(good, r);
        QVERIFY(result.valid());
        QVERIFY(!result.matches(good, r, cancelled));
    }
    void exactMoveBudgetIsAcceptedAndInputNeverMutated() {
        JobPlan plan;
        plan.moves.reserve(JobPlan::maximumMoves);
        for (int n = 0; n < JobPlan::maximumMoves; ++n)
            plan.moves.append({{0, 0}, {0, 0}, 600, 0, 1, true});
        const auto before = plan.motionFingerprint();
        const auto result = check(plan, request(plan, {0, 0}, {0, 0, 0, 0}));
        QVERIFY2(result.valid(), qPrintable(result.error()));
        equal(result.required(), {});
        QCOMPARE(plan.motionFingerprint(), before);
        QCOMPARE(plan.moves.size(), JobPlan::maximumMoves);
    }
};
QTEST_GUILESS_MAIN(ManualTravelCheckTest)
#include "manualtravelcheck_test.moc"
