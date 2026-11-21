#include "controllerjobpreparation_p.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtConcurrent>
#include <QtTest>

class ControllerJobPreparationTest : public QObject {
    Q_OBJECT
    struct Control {
        QSemaphore entered, release;
        QMutex mutex;
        QList<int> calls;
        QList<Qt::HANDLE> threads;
        int active = 0, peak = 0;
        bool timedOut = false;
    };
    static JobPlan plan(int identity = 1) {
        JobPlan p;
        p.moves = {{{20,30},{70,30},5000,100,identity,false}};
        return p;
    }
    static std::shared_ptr<Control> blockFirst(ControllerJobPreparation &preparation) {
        auto control = std::make_shared<Control>();
        QMutexLocker lock(&preparation.m_state->mutex);
        preparation.m_state->work = [control](const JobPlan &job, const Cancellation &cancel) {
            bool first;
            {
                QMutexLocker lock(&control->mutex);
                first = control->calls.isEmpty();
                control->calls << job.moves.first().layer;
                control->threads << QThread::currentThreadId();
                control->peak = std::max(control->peak, ++control->active);
            }
            if (first) {
                control->entered.release();
                if (!control->release.tryAcquire(1,3000)) control->timedOut = true;
            }
            auto result = PreparedControllerJob::prepare(job,cancel);
            { QMutexLocker lock(&control->mutex); --control->active; }
            return result;
        };
        return control;
    }
    static bool stopped(const std::shared_ptr<ControllerJobPreparationState> &state) {
        QMutexLocker lock(&state->mutex); return state->executorStopped;
    }
  private slots:
    void preparesRealImmutableOutputWithoutAutomaticExecution() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(),1));
        QTRY_COMPARE(finished.size(),1);
        QCOMPARE(finished[0][0].toULongLong(),1ULL);
        const auto result = preparation.result(1);
        QVERIFY(result && result->valid());
        QVERIFY(!preparation.preparing());
        QVERIFY(preparation.reason().isEmpty());
        QVERIFY(!preparation.result(2));
        auto changed = plan(2);
        changed.moves.replace(0, {{80,30},{90,30},1000,20,2,false});
        QVERIFY(preparation.prepare(changed,2));
        QVERIFY(!preparation.result(1));
        QTRY_COMPARE(finished.size(),2);
        QVERIFY(preparation.result(2));
        QCOMPARE(result->firstApproach(),QPointF(20,30));
        QCOMPARE(preparation.result(2)->firstApproach(),QPointF(80,30));
    }
    void burstCoalescesToOneActiveAndOnlyTheLatestPendingPlan() {
        ControllerJobPreparation preparation;
        auto control = blockFirst(preparation);
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(1),1));
        QVERIFY(control->entered.tryAcquire(1,1000));
        for (quint64 i=2;i<=100;++i) QVERIFY(preparation.prepare(plan(i),i));
        QVERIFY(preparation.preparing());
        QVERIFY(!preparation.result(1));
        control->release.release();
        QTRY_COMPARE(finished.size(),1);
        QCOMPARE(finished[0][0].toULongLong(),100ULL);
        QVERIFY(preparation.result(100) && preparation.result(100)->valid());
        {
            QMutexLocker lock(&control->mutex);
            QCOMPARE(control->calls,QList<int>({1,100}));
            QCOMPARE(control->peak,1);
            QVERIFY(!control->timedOut);
            for (auto thread: control->threads) QVERIFY(thread != QThread::currentThreadId());
        }
    }
    void cancelInvalidatesBothWorkAndQueuedCompletion() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(),1));
        // Block actual GUI delivery until the result has been published by the
        // independent compute thread, then cancel before its queued callback.
        QElapsedTimer budget; budget.start();
        while (!preparation.result(1) && budget.elapsed()<1000) QThread::msleep(1);
        QVERIFY(preparation.result(1));
        QCOMPARE(finished.size(),0);
        preparation.cancel();
        QVERIFY(!preparation.result(1));
        QVERIFY(!preparation.preparing());
        QCoreApplication::processEvents();
        QCOMPARE(finished.size(),0);
        QVERIFY(!preparation.prepare(plan(),1)); // No replay of cancelled generation.
        QVERIFY(preparation.prepare(plan(2),2));
        QTRY_COMPARE(finished.size(),1);
        QCOMPARE(finished[0][0].toULongLong(),2ULL);
    }
    void cancellingActiveAndLatestNeverRunsSupersededPending() {
        ControllerJobPreparation preparation;
        auto control=blockFirst(preparation);
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(1),1));
        QVERIFY(control->entered.tryAcquire(1,1000));
        QVERIFY(preparation.prepare(plan(2),2));
        preparation.cancel();
        QVERIFY(preparation.prepare(plan(3),3));
        control->release.release();
        QTRY_COMPARE(finished.size(),1);
        QCOMPARE(finished[0][0].toULongLong(),3ULL);
        QMutexLocker lock(&control->mutex);
        QCOMPARE(control->calls,QList<int>({1,3}));
    }
    void anOlderQueuedNotificationDeliversOnlyTheNewestCompletedGeneration() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        for (quint64 id: {1ULL,2ULL}) {
            QVERIFY(preparation.prepare(plan(id),id));
            QElapsedTimer budget; budget.start();
            while (!preparation.result(id) && budget.elapsed()<1000) QThread::msleep(1);
            QVERIFY(preparation.result(id));
            QCOMPARE(finished.size(),0); // Neither queued callback has run in the GUI.
        }
        QTRY_COMPARE(finished.size(),1);
        QCOMPARE(finished[0][0].toULongLong(),2ULL);
        QVERIFY(!preparation.result(1));
        QVERIFY(preparation.result(2));
    }
    void reentrantChangedCannotPublishThePreviousResultAsCurrent() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        bool replaced=false;
        connect(&preparation,&ControllerJobPreparation::changed,&preparation,[&] {
            if (!replaced && preparation.result(1)) {
                replaced=true;
                QVERIFY(preparation.prepare(plan(2),2));
            }
        });
        QVERIFY(preparation.prepare(plan(),1));
        QTRY_COMPARE(finished.size(),1);
        QVERIFY(replaced);
        QCOMPARE(finished[0][0].toULongLong(),2ULL);
        QVERIFY(!preparation.result(1));
        QVERIFY(preparation.result(2));
    }
    void invalidInputFinishesAsDataErrorAndNeverExposesOutput() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(JobPlan{},1));
        QTRY_COMPARE(finished.size(),1);
        const auto result=preparation.result(1);
        QVERIFY(result && !result->valid() && result->lines().isEmpty());
        QVERIFY(!preparation.reason().isEmpty());
        QVERIFY(!preparation.preparing());
    }
    void finishedIdentityMustBeRecheckedAfterAnotherReceiverChangesThePlan() {
        ControllerJobPreparation preparation;
        bool oldIdentityRejected=false;
        connect(&preparation,&ControllerJobPreparation::finished,&preparation,[&](quint64 id) {
            if (id==1) QVERIFY(preparation.prepare(plan(2),2));
        });
        connect(&preparation,&ControllerJobPreparation::finished,&preparation,[&](quint64 id) {
            if (id==1) oldIdentityRejected=!preparation.result(id);
        });
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(),1));
        QTRY_COMPARE(finished.size(),2);
        QVERIFY(oldIdentityRejected);
        QVERIFY(preparation.result(2));
    }
    void aForeignThreadCannotSubmitOrMutateTheCandidate() {
        ControllerJobPreparation preparation;
        QSignalSpy changed(&preparation,&ControllerJobPreparation::changed);
        auto future=QtConcurrent::run([&] { return preparation.prepare(plan(),1); });
        future.waitForFinished(); // Test-only barrier; production submission stays GUI-owned.
        QVERIFY(!future.result());
        QCOMPARE(preparation.generation(),0ULL);
        QVERIFY(!preparation.preparing());
        QCOMPARE(changed.size(),0);
    }
    void preparationSnapshotDoesNotRetainUnneededRasterPreviews() {
        ControllerJobPreparation preparation;
        std::atomic_bool previewWasExcluded{false};
        {
            QMutexLocker lock(&preparation.m_state->mutex);
            preparation.m_state->work=[&](const JobPlan &snapshot,const Cancellation &cancel) {
                previewWasExcluded.store(snapshot.previews.isEmpty());
                return PreparedControllerJob::prepare(snapshot,cancel);
            };
        }
        auto source=plan();
        source.previews["source-only"]=QImage(8,8,QImage::Format_ARGB32);
        const auto original=source.previews["source-only"].cacheKey();
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(source,1));
        QTRY_COMPARE(finished.size(),1);
        QVERIFY(previewWasExcluded.load());
        QCOMPARE(source.previews.size(),1);
        QCOMPARE(source.previews["source-only"].cacheKey(),original);
        QVERIFY(preparation.result(1) && preparation.result(1)->valid());
    }
    void exceptionIsAnExplicitFailureRatherThanReadyData() {
        ControllerJobPreparation preparation;
        {
            QMutexLocker lock(&preparation.m_state->mutex);
            preparation.m_state->work=[](const JobPlan &,const Cancellation &) -> PreparedControllerJob::Ptr { throw 7; };
        }
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(),1));
        QTRY_COMPARE(finished.size(),1);
        QVERIFY(!preparation.result(1));
        QVERIFY(!preparation.reason().isEmpty());
        QVERIFY(!preparation.preparing());
    }
    void destructionRevokesWithoutJoiningOrDeliveringToDeadOwner() {
        auto preparation=new ControllerJobPreparation;
        const auto state=preparation->m_state;
        auto control=blockFirst(*preparation);
        int finishedCount=0;
        connect(preparation,&ControllerJobPreparation::finished,this,[&] { ++finishedCount; });
        QVERIFY(preparation->prepare(plan(),1));
        QVERIFY(control->entered.tryAcquire(1,1000));
        QElapsedTimer elapsed; elapsed.start();
        delete preparation;
        QVERIFY2(elapsed.elapsed()<500,"Facade destruction joined the deliberately blocked compute thread.");
        {
            QMutexLocker lock(&state->mutex);
            QVERIFY(state->closed && !state->owner);
            QVERIFY(state->activeCancel && isCancelled(state->activeCancel));
        }
        control->release.release();
        QTRY_VERIFY(stopped(state));
        QCoreApplication::processEvents();
        QCOMPARE(finishedCount,0);
        QVERIFY(!control->timedOut);
    }
    void closeIsPermanentAndQueuedCallbackCannotResurrectIt() {
        ControllerJobPreparation preparation;
        QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
        QVERIFY(preparation.prepare(plan(),1));
        QElapsedTimer budget; budget.start();
        while (!preparation.result(1) && budget.elapsed()<1000) QThread::msleep(1);
        QVERIFY(preparation.result(1));
        preparation.close();
        QVERIFY(!preparation.prepare(plan(2),2));
        QVERIFY(!preparation.result(1));
        QCoreApplication::processEvents();
        QCOMPARE(finished.size(),0);
        QVERIFY(!preparation.preparing());
        QTRY_VERIFY(stopped(preparation.m_state));
    }
    void repeatedOwnerLifetimeReleasesTheComputeHost() {
        for (int i=0;i<15;++i) {
            std::weak_ptr<ControllerJobPreparationState> weak;
            {
                ControllerJobPreparation preparation;
                weak=preparation.m_state;
                QSignalSpy finished(&preparation,&ControllerJobPreparation::finished);
                QVERIFY(preparation.prepare(plan(),1));
                QTRY_COMPARE(finished.size(),1);
            }
            QTRY_VERIFY_WITH_TIMEOUT(([&] {
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                return weak.expired();
            })(),1000);
        }
    }
};
QTEST_GUILESS_MAIN(ControllerJobPreparationTest)
#include "controllerjobpreparation_test.moc"
