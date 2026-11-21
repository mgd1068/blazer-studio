#include "preparedcontrollerjob.h"
#include "job.h"
#include "grblwirenumbers.h"
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QtConcurrent>
#include <QtTest>
#include <cmath>
#include <limits>

class PreparedControllerJobTest : public QObject {
    Q_OBJECT
    static JobPlan edge() {
        JobPlan plan;
        plan.moves = {{{20,30},{70,30},5000,100,0,false}};
        return plan;
    }
    static QList<QPointF> emittedTargets(const QStringList &lines) {
        QList<QPointF> targets;
        const QRegularExpression move("^G[01] X([+-]?[0-9]+(?:\\.[0-9]+)?) Y([+-]?[0-9]+(?:\\.[0-9]+)?)(?: |$)");
        for (const auto &line : lines) {
            const auto match = move.match(line);
            if (match.hasMatch()) targets << QPointF(match.captured(1).toDouble(), match.captured(2).toDouble());
        }
        return targets;
    }
  private slots:
    void firstEmittingEdgeHasOutputOffApproachAndDegenerateBounds() {
        auto plan = edge();
        const auto result = PreparedControllerJob::prepare(plan);
        QVERIFY2(result->valid(), qPrintable(result->errors().join("; ")));
        QCOMPARE(result->workBounds(), QRectF(20,30,50,0));
        QCOMPARE(result->firstApproach(), QPointF(20,30));
        QCOMPARE(result->moveCount(), 1);
        QCOMPARE(result->profileFingerprint(), plan.deviceProfile().outputFingerprint());
        QCOMPARE(result->sourceFingerprint(), plan.motionFingerprint());
        QCOMPARE(result->driverId(), QString("grbl-serial"));
        QCOMPARE(result->transport(), QString("serial"));
        QCOMPARE(result->mediaType(), QString("text/x-gcode"));
        const auto lines = result->lines();
        const auto approach = lines.indexOf("G0 X20.000 Y30.000");
        QVERIFY(approach > lines.indexOf("S0"));
        QVERIFY(approach < lines.indexOf("M4 S1000"));
        QCOMPARE(emittedTargets(lines), QList<QPointF>({{20,30},{70,30}}));
    }
    void rapidPreviewOriginIsNotAnEmittedEndpoint() {
        JobPlan plan;
        plan.moves = {{{0,0},{70,60},3000,0,0,true},
                      {{70,60},{80,60},3000,50,0,false}};
        const auto result = PreparedControllerJob::prepare(plan);
        QVERIFY(result->valid());
        QCOMPARE(result->workBounds(), QRectF(70,60,10,0));
        QCOMPARE(result->firstApproach(), QPointF(70,60));
        QCOMPARE(emittedTargets(result->lines()), QList<QPointF>({{70,60},{80,60}}));
    }
    void preparedOutputDoesNotFollowLaterPlanMutationOrDestruction() {
        PreparedControllerJob::Ptr original;
        QByteArray fingerprint;
        {
            auto plan = edge();
            plan.cacheCommands();
            original = PreparedControllerJob::prepare(plan);
            QVERIFY(original->valid());
            fingerprint = original->sourceFingerprint();
            plan.moves.replace(0, {{50,40},{90,60},300,25,1,false});
            const auto modified = PreparedControllerJob::prepare(plan);
            QVERIFY(modified->valid());
            QVERIFY(modified->sourceFingerprint() != fingerprint);
            QCOMPARE(modified->firstApproach(), QPointF(50,40));
            QVERIFY(modified->lines() != original->lines());
            plan.errors << "later discarded draft";
        }
        QVERIFY(original->valid());
        QCOMPARE(original->sourceFingerprint(), fingerprint);
        QCOMPARE(original->firstApproach(), QPointF(20,30));
        QVERIFY(original->lines().contains("G1 X70.000 Y30.000 F5000.000"));
    }
    void invalidPlansNeverExposePartialOutput_data() {
        QTest::addColumn<int>("failure");
        for (int i=0;i<8;++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidPlansNeverExposePartialOutput() {
        QFETCH(int, failure);
        auto plan = edge();
        auto move = plan.moves.first();
        switch (failure) {
        case 0: plan.moves.clear(); break;
        case 1: move.to.setX(std::numeric_limits<double>::quiet_NaN()); plan.moves.replace(0,move); break;
        case 2: move.speed = std::numeric_limits<double>::infinity(); plan.moves.replace(0,move); break;
        case 3: move.power = 101; plan.moves.replace(0,move); break;
        case 4: move.to.setX(201); plan.moves.replace(0,move); break;
        case 5: plan.errors << "draft invalid"; break;
        case 6: plan.moves.append({{80,30},{90,30},5000,100,0,false}); break;
        case 7: plan.moves.resize(JobPlan::maximumMoves+1); break;
        }
        const auto result = PreparedControllerJob::prepare(plan);
        QVERIFY(!result->valid());
        QVERIFY(!result->errors().isEmpty());
        QVERIFY(result->lines().isEmpty());
    }
    void cancellationProducesNoReadyOutput() {
        const auto cancel = std::make_shared<std::atomic_bool>(true);
        const auto result = PreparedControllerJob::prepare(edge(), cancel);
        QVERIFY(result->cancelled());
        QVERIFY(!result->valid());
        QVERIFY(result->lines().isEmpty());
        QVERIFY(result->sourceFingerprint().isEmpty());
    }
    void deviceAndMotionBindingsRemainDistinct() {
        auto first = edge();
        auto data = first.deviceProfile().toJson();
        auto driver = data["driver"].toObject();
        auto settings = driver["settings"].toObject(); settings["powerScale"] = 255;
        driver["settings"] = settings; data["driver"] = driver;
        QString error;
        auto profile = DeviceProfile::parse(QJsonDocument(data).toJson(),error);
        QVERIFY2(profile, qPrintable(error));
        JobPlan second(*profile); second.moves = first.moves;
        auto a=PreparedControllerJob::prepare(first), b=PreparedControllerJob::prepare(second);
        QVERIFY(a->valid() && b->valid());
        QCOMPARE(a->sourceFingerprint(), b->sourceFingerprint());
        QVERIFY(a->profileFingerprint() != b->profileFingerprint());
        QVERIFY(a->lines().contains("M4 S1000"));
        QVERIFY(b->lines().contains("M4 S255"));
    }
    void compactBoundsMatchIndependentlyParsedWireTargetsWithSignedOffsets() {
        QRandomGenerator random(0xb1a2e6);
        for (int trial=0;trial<250;++trial) {
            JobPlan plan;
            QPointF point(random.bounded(200000000)/1000000.,random.bounded(150000000)/1000000.);
            for (int i=0;i<20;++i) {
                QPointF next(random.bounded(200000000)/1000000.,random.bounded(150000000)/1000000.);
                plan.moves.append({point,next,3000,50,0,i%5==0});
                point=next;
            }
            const auto result=PreparedControllerJob::prepare(plan);
            QVERIFY(result->valid());
            const auto targets=emittedTargets(result->lines());
            QVERIFY(!targets.isEmpty());
            const QPointF offset(random.bounded(400000)/1000.-200,
                                 random.bounded(400000)/1000.-200);
            const auto box=result->workBounds();
            // Alternate fitting and non-fitting physical envelopes. Otherwise
            // a large random path would make almost every random offset fail
            // and hide a vacuous always-deny implementation.
            QRectF signedEnvelope(box.left()+offset.x()-.1, box.top()+offset.y()-.1,
                                  box.width()+.2, box.height()+.2);
            if (trial%2) signedEnvelope.setLeft(box.center().x()+offset.x());
            const auto permittedEncoded = [&](QPointF wire) {
                const auto p=wire+offset; // Targets were already parsed from actual GCode.
                return p.x()>=signedEnvelope.left() && p.x()<=signedEnvelope.right() &&
                       p.y()>=signedEnvelope.top() && p.y()<=signedEnvelope.bottom();
            };
            bool everyTarget=true;
            for (auto target: targets) everyTarget &= permittedEncoded(target);
            const auto cornerPermitted = [&](QPointF raw) {
                return permittedEncoded({GrblWireNumbers::coordinate(raw.x()),
                                         GrblWireNumbers::coordinate(raw.y())});
            };
            const bool corners=cornerPermitted(box.topLeft()) && cornerPermitted(box.topRight()) &&
                               cornerPermitted(box.bottomLeft()) && cornerPermitted(box.bottomRight());
            QCOMPARE(corners,everyTarget);
            QCOMPARE(everyTarget,trial%2==0);
        }
    }
    void preparationCanRunOutsideCallerThread() {
        const auto caller=QThread::currentThread();
        auto future=QtConcurrent::run([plan=edge(),caller] {
            return std::pair{QThread::currentThread()!=caller,PreparedControllerJob::prepare(plan)};
        });
        future.waitForFinished(); // Test barrier only; production callers must be asynchronous.
        const auto result=future.result();
        QVERIFY(result.first);
        QVERIFY(result.second->valid());
    }
};
QTEST_APPLESS_MAIN(PreparedControllerJobTest)
#include "preparedcontrollerjob_test.moc"
