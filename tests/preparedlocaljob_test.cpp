#include "preparedlocaljob.h"
#include "virtualgrblplant.h"
#include <QJsonDocument>
#include <QtTest>
#include <cmath>
#include <limits>
#include <thread>

namespace {
using Distances = ManualTravelCheck::Distances;
JobPlan path(QPointF head, std::initializer_list<QPointF> points) {
    JobPlan plan;
    QPointF previous = head;
    for (auto p : points) {
        plan.moves.append({previous, p, 600, 0, 0, true});
        previous = p;
    }
    return plan;
}
ManualTravelCheck::Request request(const JobPlan &plan, QPointF head,
                                   Distances d = {50, 50, 30, 30}) {
    return {head, d, 2., ManualTravelCheck::sourceFingerprint(plan)};
}
void equal(Distances actual, Distances expected) {
    QCOMPARE(actual.negativeX, expected.negativeX);
    QCOMPARE(actual.positiveX, expected.positiveX);
    QCOMPARE(actual.negativeY, expected.negativeY);
    QCOMPARE(actual.positiveY, expected.positiveY);
}
} // namespace

class PreparedLocalJobTest : public QObject {
    Q_OBJECT
    static bool parse(QStringList lines, QPointF head = {10, 10}) {
        PreparedLocalJob result;
        result.m_head = head;
        return result.inspectWire(lines, 1000, {200, 150}, {});
    }
    static QStringList minimal() {
        return {"M5",
                "(Blazer Studio: GRBL laser mode, $30=1000, $32=1, G54)",
                "G21",
                "G90",
                "G54",
                "G17",
                "G94",
                "S0",
                "G0 X10.000 Y10.000",
                "M4 S500",
                "G1 X20.000 Y10.000 F600.000",
                "M5 S0",
                "G0"};
    }
  private slots:
    void dynamicRapidsPreserveModeAndPowerWithoutEmission() {
        JobPlan plan;
        plan.moves = {{{10, 10}, {12, 10}, 600, 20, 0, false},
                      {{12, 10}, {20, 10}, 600, 0, 0, true},
                      {{20, 10}, {22, 10}, 600, 80, 0, false},
                      {{22, 10}, {30, 11}, 600, 0, 0, true},
                      {{30, 11}, {32, 11}, 600, 30, 0, false},
                      {{32, 11}, {34, 11}, 600, 0, 0, false},
                      {{34, 11}, {34, 12}, 600, 0, 0, true},
                      {{34, 12}, {32, 12}, 600, 60, 0, false},
                      {{32, 12}, {30, 12}, 600, 0, 0, false},
                      {{30, 12}, {10, 10}, 600, 0, 0, true}};
        const auto result = PreparedLocalJob::prepare(plan, request(plan, {10, 10}, {10, 30, 10, 10}));
        QVERIFY2(result->valid(), qPrintable(result->errors().join("; ")));
        equal(result->requiredUnion(), {0, 24, 0, 2});
        equal(result->usableAfterMargin(), {8, 28, 8, 8});
        const auto lines = result->prepared()->lines();
        QCOMPARE(lines.count("M4 S200"), 1);
        QCOMPARE(lines.count("M5"), 1);
        QCOMPARE(lines.count("M5 S0"), 1);
        QVERIFY(lines.contains("G1 X22.000 Y10.000 S800"));
        QVERIFY(lines.contains("G1 X32.000 Y11.000 S300"));
        QVERIFY(lines.contains("G1 X32.000 Y12.000 S600"));
        VirtualGrblPlant plant({-100, -100, 200, 150}, {-20, -20}, {-50, -50}, {-98, -98});
        plant.counterBias = {};
        const QList<double> expectedPowers = {200, 800, 300, 0, 600, 0};
        int cutting = 0, rapids = 0;
        for (const auto &line : lines) {
            const auto before = plant.modal;
            const auto count = plant.travel.size();
            QCOMPARE(plant.apply(line.toLatin1()), VirtualGrblPlant::Result::Accepted);
            if (plant.travel.size() == count)
                continue;
            const auto &move = plant.travel.last();
            if (move.rapid) {
                QVERIFY(!move.emitting);
                if (++rapids == 1) {
                    QCOMPARE(plant.modal.spindle, 0);
                    QCOMPARE(plant.modal.power, 0.);
                } else {
                    QCOMPARE(plant.modal.spindle, 4);
                    QCOMPARE(plant.modal.power, before.power);
                }
            } else {
                QVERIFY(cutting < expectedPowers.size());
                QCOMPARE(plant.modal.spindle, 4);
                QCOMPARE(plant.modal.power, expectedPowers[cutting]);
                QCOMPARE(move.emitting, expectedPowers[cutting++] > 0);
            }
        }
        QCOMPARE(cutting, 6);
        QCOMPARE(rapids, 5);
        QCOMPARE(plant.physical, QPointF(-40, -40));
        QCOMPARE(plant.modal.spindle, 0);
        QCOMPARE(plant.modal.power, 0.);
        QCOMPARE(plant.idleOutput(), 0.);
        QCOMPARE(plant.limits, 0);
        // Only the known G0 transition is new. No S/F or foreign modes on it.
        for (const auto &bad : {"G0 X20.000 Y10.000 S0", "G0 X20.000 Y10.000 F600.000",
                                "G91 G0 X20.000 Y10.000", "G0 X20.000 Y10.000 Z0"}) {
            auto invalid = minimal();
            invalid.insert(11, bad);
            QVERIFY(!parse(invalid));
        }
    }
    void nineAnchors_data() {
        QTest::addColumn<int>("anchor");
        for (int n = 0; n < 9; ++n)
            QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void nineAnchors() {
        QFETCH(int, anchor);
        const QPointF head(100, 75);
        const double left = 100 - (anchor % 3) * 20., bottom = 75 - (anchor / 3) * 10.;
        auto plan = path(head, {{left, bottom},
                                {left + 40, bottom},
                                {left + 40, bottom + 20},
                                {left, bottom + 20},
                                {left, bottom}});
        const auto r = request(plan, head);
        const auto result = PreparedLocalJob::prepare(plan, r);
        QVERIFY2(result->valid(), qPrintable(result->errors().join("; ")));
        equal(result->requiredRaw(), {100 - left, left + 40 - 100, 75 - bottom, bottom + 20 - 75});
        equal(result->requiredActualWire(), result->requiredRaw());
        equal(result->requiredUnion(), result->requiredRaw());
        equal(result->available(), {50, 50, 30, 30});
        equal(result->usableAfterMargin(), {48, 48, 28, 28});
        QCOMPARE(result->actualAbsoluteWireBounds(), QRectF(left, bottom, 40, 20));
        QCOMPARE(result->actualLocalWireBounds(), QRectF(left - 100, bottom - 75, 40, 20));
        QCOMPARE(result->firstApproach(), QPointF(left, bottom));
        QCOMPARE(result->firstNonzeroMotionLineIndex(), qsizetype(anchor == 0 ? 9 : 8));
        QCOMPARE(result->motionLineCount(), qsizetype(5));
        QCOMPARE(result->sourceFingerprint(), plan.motionFingerprint());
        QCOMPARE(result->manualTravelSourceFingerprint(), r.expectedSourceFingerprint);
        QCOMPARE(result->profileFingerprint(), plan.deviceProfile().outputFingerprint());
        QCOMPARE(result->prepared()->lines(), plan.gcode());
        QVERIFY(result->matches(plan, r));
        QCOMPARE(result->fingerprint().size(), 32);
        QCOMPARE(result->localFrameFingerprint().size(), 32);
    }
    void shiftedActualWirePhaseRequiresMoreThanPureLocalQuantization() {
        // Independent decimal facts: raw .0004+.0044=.0048 emits X.005;
        // true displacement .005-.0004=.0046, not wire(.0044)=.004.
        const QPointF head(.0004, 10);
        const auto plan = path(head, {{.0048, 10}});
        auto r = request(plan, head, {2, 2.0045, 2, 2});
        const auto pure = ManualTravelCheck::check(plan, r);
        QVERIFY(pure.valid());
        const auto bad = PreparedLocalJob::prepare(plan, r);
        QVERIFY(!bad->valid());
        QVERIFY(!bad->prepared());
        QVERIFY(bad->errors().join(';').contains("+X"));
        QCOMPARE(bad->requiredRaw().positiveX, .0048 - .0004);
        QCOMPARE(bad->requiredActualWire().positiveX, .005 - .0004);
        QCOMPARE(bad->firstApproach(), QPointF(.005, 10));
        QCOMPARE(bad->firstNonzeroMotionLineIndex(), qsizetype(8));
        r.available->positiveX = 2.0047;
        const auto good = PreparedLocalJob::prepare(plan, r);
        QVERIFY2(good->valid(), qPrintable(good->errors().join(';')));
        QCOMPARE(good->prepared()->lines()[8], QString("G0 X0.005 Y10.000"));
        // The opposite direction has its own shifted ABS rounding phase.
        auto shiftedNegative = path({10.0006, 10}, {{9.9962, 10}});
        r = request(shiftedNegative, {10.0006, 10}, {2.0045, 2, 2, 2});
        QVERIFY(ManualTravelCheck::check(shiftedNegative, r).valid());
        const auto t = PreparedLocalJob::prepare(shiftedNegative, r);
        QVERIFY(!t->valid());
        QCOMPARE(t->requiredActualWire().negativeX, 10.0006 - 9.996);
    }
    void rawOutwardTravelIsNeverHiddenByInwardWireRounding() {
        const auto plan = path({10, 10}, {{9.9956, 10}});
        auto r = request(plan, {10, 10}, {2.0043, 2, 2, 2});
        const auto bad = PreparedLocalJob::prepare(plan, r);
        QVERIFY(!bad->valid());
        QVERIFY(!bad->prepared());
        QCOMPARE(bad->requiredRaw().negativeX, 10 - 9.9956);
        QCOMPARE(bad->requiredActualWire().negativeX, 10 - 9.996);
        r.available->negativeX = 2.0045;
        const auto good = PreparedLocalJob::prepare(plan, r);
        QVERIFY(good->valid());
        QCOMPARE(good->requiredUnion().negativeX, 10 - 9.9956);
    }
    void approachOverscanAndExistingReturnAreIncludedWithoutAddingReturn() {
        auto plan = path({50, 50}, {{40, 50}, {80, 50}, {70, 60}, {35, 50}});
        const auto r = request(plan, {50, 50}, {18, 33, 3, 13});
        const auto before = plan.gcode();
        const auto result = PreparedLocalJob::prepare(plan, r);
        QVERIFY(result->valid());
        equal(result->requiredUnion(), {15, 30, 0, 10});
        QCOMPARE(result->prepared()->lines(), before);
        QCOMPARE(result->actualAbsoluteWireBounds(), QRectF(35, 50, 45, 10));
        QCOMPARE(result->actualLocalWireBounds(), QRectF(-15, 0, 45, 10));
        QCOMPARE(result->rawLocalBounds(), QRectF(-15, 0, 45, 10));
        QCOMPARE(result->firstApproach(), QPointF(40, 50));
        auto smaller = r;
        smaller.available->negativeX = 16.999;
        QVERIFY(!PreparedLocalJob::prepare(plan, smaller)->valid());
        smaller = r;
        smaller.available->positiveX = 31.999;
        QVERIFY(!PreparedLocalJob::prepare(plan, smaller)->valid());
        QVERIFY(before.last() == "G0"); // Modal-only ending has no invented finish target.
    }
    void poweredFirstEdgeAddsLaserOffApproachAndIndexIgnoresZeroTargets() {
        JobPlan plan;
        plan.moves = {{{10, 10}, {30, 10}, 600, 50, 0, false}};
        const auto result = PreparedLocalJob::prepare(plan, request(plan, {10, 10}));
        QVERIFY(result->valid());
        QCOMPARE(result->firstApproach(), QPointF(10, 10));
        QCOMPARE(result->firstNonzeroMotionLineIndex(), qsizetype(10));
        QCOMPARE(result->prepared()->lines()[8], QString("G0 X10.000 Y10.000"));
        QCOMPARE(result->prepared()->lines()[9], QString("M4 S500"));
        QCOMPARE(result->motionLineCount(), qsizetype(2));
        auto zero = path({10, 10}, {{10, 10}, {10, 10}});
        const auto z = PreparedLocalJob::prepare(zero, request(zero, {10, 10}));
        QVERIFY(z->valid());
        QCOMPARE(z->firstNonzeroMotionLineIndex(), qsizetype(-1));
        equal(z->requiredActualWire(), {});
    }
    void foreignPlanRequestProfileAndTinyStaleChangesFailClosed() {
        auto plan = path({10, 10}, {{20, 10}, {20, 20}});
        auto r = request(plan, {10, 10});
        const auto a = PreparedLocalJob::prepare(plan, r);
        QVERIFY(a->valid());
        auto changed = plan;
        auto move = changed.moves[1];
        move.speed = 601;
        changed.moves.replace(1, move);
        QVERIFY(!a->matches(changed, r));
        QVERIFY(!PreparedLocalJob::prepare(changed, r)->valid());
        auto tiny = r;
        tiny.headAnchor->setX(10 + 1e-12);
        QVERIFY(!a->matches(plan, tiny));
        QVERIFY(!PreparedLocalJob::prepare(plan, tiny)->valid());
        for (int i = 0; i < 4; ++i) {
            auto d = r;
            switch (i) {
            case 0:
                d.available->negativeX -= 1e-12;
                break;
            case 1:
                d.available->positiveX -= 1e-12;
                break;
            case 2:
                d.available->negativeY -= 1e-12;
                break;
            case 3:
                d.available->positiveY -= 1e-12;
                break;
            }
            QVERIFY(!a->matches(plan, d));
            const auto b = PreparedLocalJob::prepare(plan, d);
            QVERIFY(b->valid());
            QVERIFY(a->fingerprint() != b->fingerprint());
            QVERIFY(a->localFrameFingerprint() != b->localFrameFingerprint());
        }
        auto data = plan.deviceProfile().toJson();
        auto driver = data["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        data["driver"] = driver;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(data).toJson(), error);
        QVERIFY(profile);
        JobPlan other(*profile);
        other.moves = plan.moves;
        QVERIFY(!a->matches(other, r));
        QVERIFY(!PreparedLocalJob::prepare(other, r)->valid());
        const auto b = PreparedLocalJob::prepare(other, request(other, {10, 10}));
        QVERIFY(b->valid());
        QCOMPARE(a->sourceFingerprint(), b->sourceFingerprint());
        QVERIFY(a->profileFingerprint() != b->profileFingerprint());
        QVERIFY(a->fingerprint() != b->fingerprint());
        QVERIFY(a->matches(plan, r));
    }
    void immutableCursorAndBindingsSurviveSourceDestruction() {
        PreparedLocalJob::Ptr saved;
        QStringList lines;
        QByteArray fingerprint;
        {
            auto plan = path({10, 10}, {{20, 10}});
            const auto r = request(plan, {10, 10});
            saved = PreparedLocalJob::prepare(plan, r);
            QVERIFY(saved->valid());
            lines = plan.gcode();
            fingerprint = saved->fingerprint();
            plan.moves.clear();
            plan.errors << "discard";
        }
        QVERIFY(saved->valid());
        QCOMPARE(saved->prepared()->lines(), lines);
        QCOMPARE(saved->fingerprint(), fingerprint);
    }
    void missingUnknownNegativeReserveAndHeadInputsNeverExposeCursor() {
        const auto plan = path({10, 10}, {{20, 10}});
        const auto base = request(plan, {10, 10});
        for (int i = 0; i < 9; ++i) {
            auto r = base;
            switch (i) {
            case 0:
                r.headAnchor.reset();
                break;
            case 1:
                r.available.reset();
                break;
            case 2:
                r.expectedSourceFingerprint.clear();
                break;
            case 3:
                r.expectedSourceFingerprint = QByteArray(32, 'x');
                break;
            case 4:
                r.available->negativeX = -1;
                break;
            case 5:
                r.available->positiveY = std::numeric_limits<double>::quiet_NaN();
                break;
            case 6:
                r.safetyMarginMm = 0;
                break;
            case 7:
                r.safetyMarginMm = 2.000000000001;
                break;
            case 8:
                r.headAnchor = QPointF(std::numeric_limits<double>::infinity(), 10);
                break;
            }
            const auto bad = PreparedLocalJob::prepare(plan, r);
            QVERIFY(!bad->valid());
            QVERIFY(!bad->prepared());
            QVERIFY(!bad->errors().isEmpty());
        }
        auto r = base;
        r.available->negativeY = 1.999;
        QVERIFY(!PreparedLocalJob::prepare(plan, r)->valid());
        auto discontinuous = plan;
        discontinuous.moves.append({{21, 10}, {22, 10}, 600, 0, 0, true});
        QVERIFY(
            !PreparedLocalJob::prepare(discontinuous, request(discontinuous, {10, 10}))->valid());
    }
    void parserRejectsUnknownCoordinatesAndTargetsAtomically_data() {
        QTest::addColumn<QString>("bad");
        for (const auto s :
             {"G53 G0 X10.000 Y10.000", "G91", "G20", "G55", "G92 X0", "G0 X10.000", "G0 Y10.000",
              "G0 X10.000 Y10.000 Z0", "G1 XNaN Y10.000 F600.000", "G2 X10.000 Y10.000 I1.000",
              "G0 X10.000 Y10.000 A1", "G0 X10.000 Y10.000 ;tail", "G0 X-0.001 Y10.000",
              "G0 X200.001 Y10.000", "M8", "M9", "G0", "(unknown)"})
            QTest::newRow(s) << QString(s);
    }
    void parserRejectsUnknownCoordinatesAndTargetsAtomically() {
        QFETCH(QString, bad);
        auto lines = minimal();
        lines[8] = bad;
        QVERIFY(!parse(lines));
        QVERIFY(parse(minimal()));
    }
    void parserIgnoresKnownCommentsAndBareModalEndingButNoModeChanges() {
        auto lines = minimal();
        lines.insert(10, "(Relief slice 1)");
        QVERIFY(parse(lines));
        lines = minimal();
        lines[3] = "G91";
        QVERIFY(!parse(lines));
        lines = minimal();
        lines[9] = "M3 S500";
        QVERIFY(!parse(lines));
        lines = minimal();
        lines[10] = "G1 X20.000 Y10.000";
        QVERIFY(!parse(lines));
        lines = minimal();
        lines[10] = "G1 X20.000 Y10.000 F600.000 S400";
        QVERIFY(!parse(lines));
        lines = minimal();
        lines[9] = "M4 S1001";
        QVERIFY(!parse(lines));
        lines = minimal();
        lines.removeLast();
        QVERIFY(!parse(lines));
        QVERIFY(!parse(minimal(), {std::numeric_limits<double>::quiet_NaN(), 0}));
    }
    void preparedFrameUsesOnlyFrozenZeroPowerTargetsAndReturnsToHead() {
        const auto plan = path({50, 50}, {{40, 50}, {80, 50}, {70, 60}, {35, 50}});
        const auto r = request(plan, {50, 50}, {18, 33, 3, 13});
        const auto result = PreparedLocalJob::prepare(plan, r);
        QVERIFY(result->valid());
        QVERIFY2(result->frameError().isEmpty(), qPrintable(result->frameError()));
        const QStringList expected = {"M5 S0",
                                      "G21",
                                      "G90",
                                      "G54",
                                      "G1 X35.000 Y50.000 S0 F600.000",
                                      "G1 X80.000 Y50.000 S0 F600.000",
                                      "G1 X80.000 Y60.000 S0 F600.000",
                                      "G1 X35.000 Y60.000 S0 F600.000",
                                      "G1 X35.000 Y50.000 S0 F600.000",
                                      "G1 X50.000 Y50.000 S0 F600.000",
                                      "M5 S0"};
        QCOMPARE(result->frameLines(), expected);
        equal(result->frameRequiredUnion(), {15, 30, 0, 10});
        QCOMPARE(result->frameFingerprint().size(), 32);
        QCOMPARE(result->prepared()->lines(), plan.gcode());
        const auto repeated = PreparedLocalJob::prepare(plan, r);
        QCOMPARE(result->frameFingerprint(), repeated->frameFingerprint());
        auto changed = plan;
        auto move = changed.moves[0];
        move.speed = 601;
        changed.moves.replace(0, move);
        const auto different =
            PreparedLocalJob::prepare(changed, request(changed, {50, 50}, {18, 33, 3, 13}));
        QVERIFY(different->valid());
        QCOMPARE(different->frameLines(), expected);
        QVERIFY(different->frameFingerprint() !=
                result->frameFingerprint()); // Same visible frame, different frozen job.
    }
    void frameReturnFractionalHeadCanFailWhileTheJobStaysValid_data() {
        QTest::addColumn<double>("headX");
        QTest::addColumn<double>("targetX");
        QTest::addColumn<bool>("positiveReturn");
        QTest::newRow("negative-round-in-return") << 10.0004 << 20.0004 << false;
        QTest::newRow("positive-round-out-return") << 10.0006 << 9.0006 << true;
    }
    void frameReturnFractionalHeadCanFailWhileTheJobStaysValid() {
        QFETCH(double, headX);
        QFETCH(double, targetX);
        QFETCH(bool, positiveReturn);
        const auto plan = path({headX, 10}, {{targetX, 10}});
        auto r = request(plan, {headX, 10},
                         positiveReturn ? Distances{4, 2, 2, 2} : Distances{2, 13, 2, 2});
        const auto result = PreparedLocalJob::prepare(plan, r);
        QVERIFY2(result->valid(), qPrintable(result->errors().join(';')));
        QVERIFY(result->prepared());
        QVERIFY(result->matches(plan, r));
        QVERIFY(result->frameLines().isEmpty());
        QVERIFY(result->frameFingerprint().isEmpty());
        QVERIFY(result->frameError().contains("Rückkehr"));
        if (positiveReturn)
            QCOMPARE(result->frameRequiredUnion().positiveX, 10.001 - headX);
        else
            QCOMPARE(result->frameRequiredUnion().negativeX, headX - 10.000);
        // Extra measured room, not changing/rounding the confirmed head, permits the frame.
        if (positiveReturn)
            r.available->positiveX = 2.001;
        else
            r.available->negativeX = 2.001;
        const auto allowed = PreparedLocalJob::prepare(plan, r);
        QVERIFY(allowed->valid());
        QVERIFY2(allowed->frameError().isEmpty(), qPrintable(allowed->frameError()));
        QCOMPARE(allowed->frameLines().size(), 11);
        QCOMPARE(allowed->headAnchor(), QPointF(headX, 10));
        QCOMPARE(allowed->frameLines()[9], positiveReturn
                                               ? QString("G1 X10.001 Y10.000 S0 F600.000")
                                               : QString("G1 X10.000 Y10.000 S0 F600.000"));
        QCOMPARE(allowed->prepared()->lines(), result->prepared()->lines());
    }
    void degenerateFramesAndInvalidJobsNeverInventMotionOrGrant() {
        const auto point = path({10, 10}, {{10, 10}});
        const auto p = PreparedLocalJob::prepare(point, request(point, {10, 10}, {2, 2, 2, 2}));
        QVERIFY(p->valid());
        QVERIFY(p->frameError().isEmpty());
        QCOMPARE(p->frameLines().size(), 11);
        for (int i = 4; i < 10; ++i)
            QCOMPARE(p->frameLines()[i], QString("G1 X10.000 Y10.000 S0 F600.000"));
        equal(p->frameRequiredUnion(), {});
        QCOMPARE(p->firstNonzeroMotionLineIndex(), qsizetype(-1));
        auto line = path({10, 10}, {{10, 20}});
        const auto l = PreparedLocalJob::prepare(line, request(line, {10, 10}));
        QVERIFY(l->valid());
        QVERIFY(l->frameError().isEmpty());
        for (int i = 4; i < 10; ++i)
            QVERIFY(l->frameLines()[i].startsWith("G1 X10.000 Y"));
        const auto invalid = PreparedLocalJob::prepare(line, request(line, {11, 10}));
        QVERIFY(!invalid->valid());
        QVERIFY(invalid->frameLines().isEmpty());
        QVERIFY(invalid->frameFingerprint().isEmpty());
        QVERIFY(!invalid->frameError().isEmpty());
    }
    void fullMoveBudgetAndCancellationNeverPublishPartialPreparation() {
        JobPlan plan;
        plan.moves.reserve(JobPlan::maximumMoves);
        for (int i = 0; i < JobPlan::maximumMoves; ++i)
            plan.moves.append({{10, 10}, {10, 10}, 600, 0, 0, true});
        const auto r = request(plan, {10, 10}, {2, 2, 2, 2});
        const auto before = plan.motionFingerprint();
        const auto good = PreparedLocalJob::prepare(plan, r);
        QVERIFY2(good->valid(), qPrintable(good->errors().join(';')));
        QCOMPARE(good->motionLineCount(), qsizetype(500000));
        QCOMPARE(good->firstNonzeroMotionLineIndex(), qsizetype(-1));
        QCOMPARE(plan.motionFingerprint(), before);
        equal(good->requiredUnion(), {});
        const auto cancelled = std::make_shared<std::atomic_bool>(true);
        const auto bad = PreparedLocalJob::prepare(plan, r, cancelled);
        QVERIFY(bad->cancelled());
        QVERIFY(!bad->prepared());
        QVERIFY(!good->matches(plan, r, cancelled));
        cancelled->store(false);
        std::thread interrupter([cancelled] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cancelled->store(true);
        });
        const auto interrupted = PreparedLocalJob::prepare(plan, r, cancelled);
        interrupter.join();
        QVERIFY(interrupted->cancelled());
        QVERIFY(!interrupted->valid());
        QVERIFY(!interrupted->prepared());
        plan.moves.append({{10, 10}, {10, 10}, 600, 0, 0, true});
        const auto oversized = PreparedLocalJob::prepare(plan, r);
        QVERIFY(!oversized->valid());
        QVERIFY(!oversized->prepared());
    }
};
QTEST_GUILESS_MAIN(PreparedLocalJobTest)
#include "preparedlocaljob_test.moc"
