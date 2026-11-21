#include "jobplacement.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QLineF>
#include <QtTest>
#include <limits>
using namespace JobPlacement;

class JobPlacementTest : public QObject {
    Q_OBJECT
    static QByteArray geometry() { return QByteArray(64, 'a'); }
    static JobPlan rectangle() {
        JobPlan plan;
        plan.moves = {{{0, 0}, {10, 20}, 3000, 0, 1, true},
                      {{10, 20}, {30, 20}, 120, 25, 1, false, 4},
                      {{30, 20}, {30, 30}, 240, 50, 1, false, 4},
                      {{30, 30}, {10, 30}, 360, 75, 1, false, -4},
                      {{10, 30}, {10, 20}, 480, 25, 1, false, -4}};
        return plan;
    }
    static PositionSnapshot position(QPointF point, const JobPlan &plan, quint64 revision = 1) {
        return {point, true, plan.deviceProfile().outputFingerprint(), QByteArray(64, 'b'),
                revision};
    }
    static SourceSnapshot source(const JobPlan &plan, QRectF bounds = {10, 20, 20, 10},
                                 OriginScope scope = OriginScope::Design) {
        return SourceSnapshot::capture(plan, bounds, geometry(), scope);
    }
    static Request current(const SourceSnapshot &snapshot, const JobPlan &plan) {
        Request request;
        request.mode = Mode::CurrentPosition;
        request.expectedSourceFingerprint = snapshot.fingerprint();
        request.startPosition = position({100, 80}, plan);
        return request;
    }
  private slots:
    void allNineAnchorsMatchIndependentCornerAndCenterExamples() {
        const auto plan = rectangle();
        const auto snapshot = source(plan);
        QVERIFY(snapshot.valid());
        const QList<QPointF> expectedTranslations{{90, 60}, {80, 60}, {70, 60}, {90, 55}, {80, 55},
                                                  {70, 55}, {90, 50}, {80, 50}, {70, 50}};
        for (int anchor = 0; anchor < 9; ++anchor) {
            auto request = current(snapshot, plan);
            request.anchor = Anchor(anchor);
            const auto placed = place(plan, snapshot, request);
            QVERIFY2(placed.valid(), qPrintable(placed.error()));
            QCOMPARE(placed.translation(), expectedTranslations[anchor]);
            QCOMPARE(placed.plan().moves.size(), plan.moves.size());
            QCOMPARE(placed.plan().moves.first().from, QPointF(100, 80));
            QCOMPARE(placed.plan().moves.first().power, 0.);
            QVERIFY(placed.plan().moves.first().rapid);
            for (qsizetype i = 0; i < plan.moves.size(); ++i) {
                const auto &original = plan.moves[i];
                const auto &move = placed.plan().moves[i];
                QCOMPARE(move.to, original.to + expectedTranslations[anchor]);
                if (i > 0)
                    QCOMPARE(move.from, original.from + expectedTranslations[anchor]);
                QCOMPARE(move.speed, original.speed);
                QCOMPARE(move.power, original.power);
                QCOMPARE(move.rapid, original.rapid);
                QCOMPARE(move.layer, original.layer);
                QCOMPARE(move.slice, original.slice);
            }
            QCOMPARE(placed.plan().workLength, 60.);
            QVERIFY(placed.matches(plan, snapshot, request));
        }
        auto centerRequest = current(snapshot, plan);
        centerRequest.anchor = Anchor::Center;
        QCOMPARE(place(plan, snapshot, centerRequest).placedGraphicsBounds(),
                 QRectF(90, 75, 20, 10));
        auto topRight = current(snapshot, plan);
        topRight.anchor = Anchor::UpperRight;
        QCOMPARE(place(plan, snapshot, topRight).placedGraphicsBounds(), QRectF(80, 70, 20, 10));
    }
    void absoluteCoordinatesIgnoreAnchorsAndUseOnlyExplicitApproach() {
        const auto plan = rectangle();
        const auto snapshot = source(plan);
        Request request;
        request.expectedSourceFingerprint = snapshot.fingerprint();
        for (int anchor = 0; anchor < 9; ++anchor) {
            request.anchor = Anchor(anchor);
            const auto placed = place(plan, snapshot, request);
            QVERIFY(placed.valid());
            QVERIFY(!placed.explicitApproach());
            QCOMPARE(placed.translation(), QPointF());
            QCOMPARE(placed.plan().motionFingerprint(), plan.motionFingerprint());
        }
        request.startPosition = position({150, 140}, plan);
        const auto placed = place(plan, snapshot, request);
        QVERIFY(placed.valid());
        QVERIFY(placed.explicitApproach());
        QCOMPARE(placed.plan().moves.first().from, QPointF(150, 140));
        QCOMPARE(placed.plan().moves.first().to, QPointF(10, 20));
        QCOMPARE(placed.plan().travelLength, QLineF({150, 140}, {10, 20}).length());
        QCOMPARE(placed.plan().workLength, 60.);
        QCOMPARE(placed.plan().moves.last().to, QPointF(10, 20)); // No automatic finish/return.
    }
    void userOriginIsIndependentOfCurrentHeadAndNeverConnectsWithPower() {
        JobPlan plan;
        plan.moves = {{{0, 0}, {20, 0}, 150, 45, 1, false, 8}};
        const auto snapshot = source(plan, {0, 0, 20, 0});
        QVERIFY(snapshot.valid()); // A horizontal line has a real degenerate anchor box.
        Request request;
        request.mode = Mode::UserOrigin;
        request.expectedSourceFingerprint = snapshot.fingerprint();
        request.startPosition = position({100, 100}, plan, 9);
        request.userOrigin = position({50, 50}, plan, 2);
        const auto placed = place(plan, snapshot, request);
        QVERIFY2(placed.valid(), qPrintable(placed.error()));
        QCOMPARE(placed.translation(), QPointF(50, 50));
        QCOMPARE(placed.plan().moves.size(), 2);
        const auto approach = placed.plan().moves.first();
        QCOMPARE(approach.from, QPointF(100, 100));
        QCOMPARE(approach.to, QPointF(50, 50));
        QVERIFY(approach.rapid);
        QCOMPARE(approach.power, 0.);
        const auto processing = placed.plan().moves.last();
        QCOMPARE(processing.from, QPointF(50, 50));
        QCOMPARE(processing.to, QPointF(70, 50));
        QCOMPARE(processing.power, 45.);
        QCOMPARE(processing.speed, 150.);
        QCOMPARE(processing.slice, 8);
        QCOMPARE(placed.plan().workLength, 20.);
        QCOMPARE(placed.plan().travelLength, std::sqrt(5000.));
        request.userOrigin = position({0, 0}, plan);
        QVERIFY(place(plan, snapshot, request).valid()); // Explicit known zero is allowed.
        request.userOrigin->known = false;
        QVERIFY(!place(plan, snapshot, request).valid()); // Merely reported zero is not reference.
    }
    void designVersusSelectionScopeIsExplicitAndFingerprintBound() {
        const auto plan = rectangle();
        const auto design = source(plan, {0, 0, 100, 100}, OriginScope::Design);
        const auto selection = source(plan, {10, 20, 20, 10}, OriginScope::Selection);
        QVERIFY(design.valid());
        QVERIFY(selection.valid());
        QVERIFY(design.fingerprint() != selection.fingerprint());
        auto request = current(design, plan);
        QCOMPARE(place(plan, design, request).translation(), QPointF(100, 80));
        QVERIFY(!place(plan, selection, request).valid());
        request.expectedSourceFingerprint = selection.fingerprint();
        QCOMPARE(place(plan, selection, request).translation(), QPointF(90, 60));
        QCOMPARE(design.scope(), OriginScope::Design);
        QCOMPARE(selection.scope(), OriginScope::Selection);
    }
    void overscanAndActualApproachMustRemainInsideWorkspace() {
        JobPlan plan;
        plan.moves = {{{0, 0}, {8, 20}, 3000, 0, 0, true},
                      {{8, 20}, {10, 20}, 1200, 0, 0, false},
                      {{10, 20}, {30, 20}, 1200, 20, 0, false},
                      {{30, 20}, {32, 20}, 1200, 0, 0, false}};
        const auto snapshot = source(plan, {10, 20, 20, 0});
        QVERIFY(snapshot.valid());
        auto request = current(snapshot, plan);
        for (const auto point :
             {QPointF(0, 20), QPointF(180, 20), QPointF(-1, 20), QPointF(201, 20)}) {
            request.startPosition = position(point, plan);
            const auto rejected = place(plan, snapshot, request);
            QVERIFY(!rejected.valid());
            QVERIFY(rejected.plan().moves.isEmpty());
        }
        request.startPosition = position({5, 20}, plan);
        const auto placed = place(plan, snapshot, request);
        QVERIFY(placed.valid());
        QCOMPARE(placed.plan().moves.first().from, QPointF(5, 20));
        QCOMPARE(placed.plan().moves.first().to, QPointF(3, 20));
        QCOMPARE(placed.plan().moves.last().to, QPointF(27, 20));
        QCOMPARE(placed.plan().moves[1].power, 0.);
        QCOMPARE(placed.plan().moves[3].power, 0.);
    }
    void changedSourceGeometryProfileRequestOrPositionCannotReusePlacement() {
        auto plan = rectangle();
        const auto snapshot = source(plan);
        auto request = current(snapshot, plan);
        const auto placed = place(plan, snapshot, request);
        QVERIFY(placed.valid());
        auto edited = plan;
        auto move = edited.moves[1];
        move.power = 26;
        edited.moves.replace(1, move);
        QVERIFY(!place(edited, snapshot, request).valid());
        QVERIFY(!placed.matches(edited, snapshot, request));
        const auto differentGeometry = SourceSnapshot::capture(
            plan, snapshot.graphicsBounds(), QByteArray(64, 'c'), OriginScope::Design);
        QVERIFY(differentGeometry.valid());
        QVERIFY(!place(plan, differentGeometry, request).valid());
        QVERIFY(!snapshot.matches(plan, QByteArray(64, 'c')));
        request.startPosition->revision++;
        QVERIFY(!placed.matches(plan, snapshot, request));
        request.startPosition->revision--;
        request.anchor = Anchor::Center;
        QVERIFY(!placed.matches(plan, snapshot, request));
        auto profileJson = plan.deviceProfile().toJson();
        auto workspace = profileJson["workspace"].toObject();
        workspace["width"] = 190;
        profileJson["workspace"] = workspace;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(profileJson).toJson(), error);
        QVERIFY2(profile.has_value(), qPrintable(error));
        JobPlan other(*profile);
        other.moves = plan.moves;
        QVERIFY(!place(other, snapshot, request).valid());
    }
    void malformedUnknownPositionAndDifferentCoordinateFrameAreRejected() {
        const auto plan = rectangle();
        const auto snapshot = source(plan);
        auto request = current(snapshot, plan);
        request.startPosition.reset();
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.startPosition->known = false;
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.startPosition->point.setX(std::numeric_limits<double>::quiet_NaN());
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.startPosition->profileFingerprint = QByteArray(64, 'f');
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.startPosition->revision = 0;
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.mode = Mode::UserOrigin;
        QVERIFY(!place(plan, snapshot, request).valid());
        request.userOrigin = position({50, 50}, plan);
        request.userOrigin->coordinateFrameFingerprint = QByteArray(64, 'c');
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.mode = Mode(99);
        QVERIFY(!place(plan, snapshot, request).valid());
        request = current(snapshot, plan);
        request.anchor = Anchor(99);
        QVERIFY(!place(plan, snapshot, request).valid());
        QVERIFY(!SourceSnapshot::capture(plan, {10, 20, -1, 1}, geometry(), OriginScope::Design)
                     .valid());
        QVERIFY(!SourceSnapshot::capture(plan, {10, 20, 1, 1}, "bad", OriginScope::Design).valid());
        QVERIFY(
            !SourceSnapshot::capture(plan, {10, 20, 1, 1}, geometry(), OriginScope(99)).valid());
        auto disconnected = plan;
        auto move = disconnected.moves[1];
        move.from = {80, 80};
        disconnected.moves.replace(1, move);
        QVERIFY(!source(disconnected).valid());
        auto burningRapid = plan;
        move = burningRapid.moves.first();
        move.power = 10;
        burningRapid.moves.replace(0, move);
        QVERIFY(!source(burningRapid).valid());
        auto stationary = plan;
        move = stationary.moves[1];
        move.to = move.from;
        stationary.moves.replace(1, move);
        QVERIFY(!source(stationary).valid());
    }
    void sourceAndCommandCacheRemainUnchangedAndCancellationIsBounded() {
        auto plan = rectangle();
        plan.cacheCommands();
        QVERIFY(!plan.cachedCommands().isEmpty());
        const auto motion = plan.motionFingerprint();
        const auto commands = plan.cachedCommands();
        const auto snapshot = source(plan);
        const auto request = current(snapshot, plan);
        const auto placed = place(plan, snapshot, request);
        QVERIFY(placed.valid());
        QVERIFY(placed.plan().cachedCommands().isEmpty());
        QCOMPARE(plan.motionFingerprint(), motion);
        QCOMPARE(plan.cachedCommands(), commands);
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!SourceSnapshot::capture(plan, {10, 20, 20, 10}, geometry(), OriginScope::Design,
                                         cancel)
                     .valid());
        QVERIFY(!place(plan, snapshot, request, cancel).valid());
        JobPlan full;
        full.moves.reserve(JobPlan::maximumMoves);
        for (int i = 0; i < JobPlan::maximumMoves; ++i)
            full.moves.append({{double(i % 2), 0}, {double((i + 1) % 2), 0}, 1000, 10, 1, false});
        const auto maximum = source(full, {0, 0, 1, 0});
        QVERIFY(maximum.valid());
        auto fullRequest = current(maximum, full);
        QVERIFY(!place(full, maximum, fullRequest).valid()); // Added approach cannot exceed cap.
        full.moves.append({{0, 0}, {1, 0}, 1000, 10, 1, false});
        QVERIFY(!source(full, {0, 0, 1, 0}).valid());
    }
};
QTEST_MAIN(JobPlacementTest)
#include "jobplacement_test.moc"
