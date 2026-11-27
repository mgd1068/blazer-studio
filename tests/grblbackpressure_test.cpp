#include "grbl.h"
#include "preparedcontrollerjob.h"
#include "support/runtimepeer.h"
#include <QtTest>
#include <QJsonArray>
using RuntimeTestSupport::RuntimePeer;

// Explicit private commissioning evidence is confined to a separately created,
// provenance-checked PTY. No actual endpoint or hardware grant can be supplied.
class GrblBackpressureTest:public QObject {
    Q_OBJECT
    static int count(RuntimePeer &peer,const QString &name){return peer.snapshot().object.value("counts").toObject().value(name).toInt();}
    static PreparedControllerJob::Ptr plan(int rows) {
        JobPlan job;QPointF from(10,10);
        for(int i=0;i<rows;++i) {
            const QPointF to(i%2?10:100,11+i);
            job.moves.append({from,to,1500,50,0,false});from=to;
        }
        return PreparedControllerJob::prepare(job);
    }
    void open(RuntimePeer &peer,GrblSession &core) {
        QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/grbl_backpressure_peer.py")).ok);
        QVERIFY(core.open(peer.port()));QTRY_VERIFY(core.ready()&&core.verified());
        core.commissionedEnvelope=QRectF(0,0,200,150);
        core.commissionedModalContract=true;core.positionReferenced=true;
    }
private slots:
    void plannerAdmissionProgressAndActualFinalM5Fence() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        const auto job=plan(8);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        // 8x3.60s motion, four finite planner slots. A queued command reaches
        // >10s write age while genuine FIFO ACKs still progress every3.6s.
        QTRY_VERIFY_WITH_TIMEOUT(!core.busy(),45000);
        QVERIFY2(core.connected(),qPrintable(core.logEntries().join(" | ")));
        QCOMPARE(core.acked,core.total);QCOMPARE(count(peer,"motionCompleted"),8);
        QCOMPARE(count(peer,"finalM5"),1);QCOMPARE(count(peer,"prematureM5"),0);
        QCOMPARE(count(peer,"collision"),0);QVERIFY(!core.bufferedJobDrain);
        bool olderThanTenSeconds=false;
        for(const auto &item:peer.snapshot().object.value("events").toArray()) {
            const auto event=item.toObject();
            if(event.value("kind")=="motionAdmitted")
                olderThanTenSeconds|=event.value("value").toObject().value("writeAgeMs").toDouble()>10000.;
        }
        QVERIFY(olderThanTenSeconds); // This exact condition fails write+10s.
        core.close();
    }
    void cachedIdleSpeedIsOnlyAcceptedForActualM5Shutdown_data() {
        QTest::addColumn<bool>("stuck");QTest::newRow("clears-on-m5")<<false;QTest::newRow("nonzero-after-m5-fails")<<true;
    }
    void cachedIdleSpeedIsOnlyAcceptedForActualM5Shutdown() {
        QFETCH(bool,stuck);RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"cachedIdleSpeed",true},{"stuckAfterM5",stuck}}).ok);
        const auto job=plan(1);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        if(stuck) {
            QTRY_VERIFY_WITH_TIMEOUT(!core.connected(),7000);
            QVERIFY(core.logEntries().join(" ").contains("Leistung im Leerlauf"));
            QVERIFY(core.progress<1.);
        } else {
            QTRY_VERIFY_WITH_TIMEOUT(!core.busy(),7000);
            QVERIFY2(core.connected(),qPrintable(core.logEntries().join(" | ")));
            QCOMPARE(core.progress,1.);QCOMPARE(core.reportedOutput,0.);
        }
        QVERIFY(count(peer,"idleCachedSpeed")>0);QCOMPARE(count(peer,"finalM5"),1);
        QCOMPARE(count(peer,"prematureM5"),0);core.close();
    }
    void missingMotionAcknowledgementsStillFailWithHealthyStatus() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"dropMotionAck",true}}).ok);
        const auto job=plan(8);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY_WITH_TIMEOUT(!core.connected(),12500);
        QVERIFY(core.logEntries().join(" ").contains("GRBL-Quittierung"));
        QVERIFY(count(peer,"statusReply")>20);QCOMPARE(count(peer,"finalM5"),0);
        QVERIFY(!core.bufferedJobDrain);
    }
    void partialOldStatusMustFinishBeforeArmingNewDrainQuery() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"freeze",true}}).ok);
        const auto job=plan(1);QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY(core.bufferedJobDrain && core.bufferedJobDrain->phase==GrblSession::BufferedJobDrain::Phase::BeforeM5);
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"statusEnabled",false}}}}).ok);
        core.poll.stop();QTest::qWait(30);
        core.bufferedJobDrain->queryArmed=false;
        core.input="<Run|MPos:10,10,0|WCO:0,0,0|FS:0,";
        core.armBufferedJobReadback(core.bufferedDrainEpoch);
        QVERIFY(core.connected());QVERIFY(!core.bufferedJobDrain->queryArmed);
        QCOMPARE(count(peer,"finalM5"),0);
        QVERIFY(peer.control({{"action","reply"},{"data","0>\r\n"}}).ok);
        QTRY_VERIFY(core.input.isEmpty() && core.bufferedJobDrain->queryArmed);
        QVERIFY(core.connected());QCOMPARE(count(peer,"finalM5"),0);
        core.stop();core.close();
    }
    void stationaryFinalDrainHasIndependentPositionDeadline() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"freeze",true}}).ok);
        const auto job=plan(1);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY(core.bufferedJobDrain && core.bufferedJobDrain->phase==GrblSession::BufferedJobDrain::Phase::BeforeM5);
        QVERIFY(core.ackDue.isEmpty()); // No missing ACK; planner never moves.
        QTRY_VERIFY_WITH_TIMEOUT(!core.connected(),12500);
        QVERIFY(core.logEntries().join(" ").contains("zehn Sekunden ohne neue gemeldete Maschinenbewegung"));
        QCOMPARE(count(peer,"finalM5"),0);QVERIFY(!core.bufferedJobDrain);
    }
    void finalDrainHardBudgetIsIndependentOfPositionDeadline() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"freeze",true}}).ok);
        const auto job=plan(1);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY(core.bufferedJobDrain && core.bufferedJobDrain->phase==GrblSession::BufferedJobDrain::Phase::BeforeM5);
        // Test-only shortening verifies the separately enforced finite ceiling;
        // it cannot authorize output or change any production timeout policy.
        core.bufferedJobDrain->hardBudgetMs=200;
        QTRY_VERIFY_WITH_TIMEOUT(!core.connected(),1500);
        QVERIFY(core.logEntries().join(" ").contains("begrenzte Planzeit"));
        QCOMPARE(count(peer,"finalM5"),0);QVERIFY(!core.bufferedJobDrain);
    }
    void holdPausesOnlyFinalDrainClockUntilExplicitResume() {
        // A FIFO ACK received during Hold must not bank paused elapsed time
        // into the next command's post-resume inactivity allowance.
        {
            GrblSession ackCore;ackCore.buffering=true;
            const auto pausedAt=ackCore.ackClock.elapsed();ackCore.ackPausedAt=pausedAt;
            ackCore.ackDue={pausedAt+10000,pausedAt+10000};
            QTest::qSleep(120);ackCore.acknowledgeDeadline();
            QCOMPARE(ackCore.ackDue.first(),pausedAt+10000);
            ackCore.clearAckDeadline();
        }
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"freeze",true}}).ok);
        const auto job=plan(1);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY(core.bufferedJobDrain && core.bufferedJobDrain->phase==GrblSession::BufferedJobDrain::Phase::BeforeM5);
        core.pause();QTRY_COMPARE(core.controllerState,QString("Hold"));QVERIFY(core.paused);
        const auto elapsed=core.bufferedJobDrain->activeMs,stalled=core.bufferedJobDrain->noProgressMs;
        const auto queries=count(peer,"query");QTRY_VERIFY(count(peer,"query")>=queries+3);
        QCOMPARE(core.bufferedJobDrain->activeMs,elapsed);QCOMPARE(core.bufferedJobDrain->noProgressMs,stalled);
        QCOMPARE(count(peer,"finalM5"),0);QVERIFY(core.connected());
        core.pause();QTRY_VERIFY(!core.paused);
        QTRY_VERIFY(core.bufferedJobDrain->activeMs>elapsed);
        core.stop();QVERIFY(!core.bufferedJobDrain);core.close();
    }
    void stopCancelsUnsentFinalM5AndDrainWatchdog() {
        RuntimePeer peer;GrblSession core;open(peer,core);if(QTest::currentTestFailed())return;
        QVERIFY(peer.control({{"action","plannerConfig"},{"freeze",true}}).ok);
        const auto job=plan(1);QVERIFY(job->valid());QVERIFY(core.startPrepared(*job));
        QTRY_VERIFY(core.bufferedJobDrain && core.bufferedJobDrain->phase==GrblSession::BufferedJobDrain::Phase::BeforeM5);
        core.stop();QVERIFY(!core.bufferedJobDrain);QVERIFY(!core.bufferedDrainWatchdog.isActive());
        QCOMPARE(count(peer,"finalM5"),0);QVERIFY(!core.positionReferenced);
        core.close();
    }
};
QTEST_MAIN(GrblBackpressureTest)
#include "grblbackpressure_test.moc"
