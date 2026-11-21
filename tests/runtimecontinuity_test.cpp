#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "controllerruntimeworker.h"
#include "support/runtimepeer.h"
#include <QJsonArray>
#include <QSemaphore>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QtTest>
#include <atomic>
#include <thread>
using RuntimeTestSupport::RuntimePeer;

class ControllerRuntimeTestAccess {
  public:
    static inline quint64 sequence=0;
    static bool start(ControllerRuntimeSession &r,PreparedControllerJob::Ptr prepared) {
        const auto status=r.status();const auto now=RuntimePeer::monotonicNs()/1000000;
        const auto digest=ControllerNativeDigest::request(ControllerNativeAction::StartJob,0,0,{},prepared->sourceFingerprint()+prepared->profileFingerprint());
        const auto input=r.sealNativeAction(ControllerNativeAction::StartJob,++sequence,now,now+250,digest,
            std::make_shared<NativeInputContextToken>(),status.connectionEpoch,status.coordinateEpoch);
        return r.submitNativeAction(ControllerNativeAction::StartJob,input,0,0,{},std::move(prepared));
    }
    template<class Function> static bool run(ControllerRuntimeSession &runtime,Function fn) {
        struct State { QSemaphore done; std::atomic_bool canceled{false}; bool ok=false; };
        auto state=std::make_shared<State>(); auto worker=runtime.m_worker;
        if (!worker || !QMetaObject::invokeMethod(worker,[worker,state,fn] {
            if (!state->canceled && worker->m_core) state->ok=fn(*worker);
            state->done.release();
        },Qt::QueuedConnection)) return false;
        if (!state->done.tryAcquire(1,3000)) { state->canceled=true; return false; }
        return state->ok;
    }
    static std::shared_ptr<std::atomic<qint64>> syntheticClock(ControllerRuntimeSession &r) {
        auto shift=std::make_shared<std::atomic<qint64>>(0);
        if (!run(r,[shift](ControllerRuntimeWorker &w) {
            w.m_clockSample=[shift] { const auto gap=1000000000LL+shift->load();
                return std::optional<ControllerRuntimeWorker::ClockInterval>{{gap,gap+10}}; };
            w.m_clockBaseline.reset(); return w.checkClockContinuity();
        })) return {};
        return shift;
    }
    static bool commission(ControllerRuntimeSession &r) {
        return run(r,[](ControllerRuntimeWorker &w) {
            auto &core=*w.m_core; if (!core.ready()) return false;
            core.commissionedEnvelope=QRectF(0,0,200,150);
            core.commissionedModalContract=true; core.positionReferenced=true;
            constexpr auto role=AccessoryContract::Role::EnclosureExtraction;
            if (!w.m_accessory.configure(role,{"private-offline-output","synthetic continuity test",true})) return false;
            auto request=w.m_accessory.request(role,true);
            if (!request || !w.m_accessory.acknowledge(*request,true)) return false;
            const auto now=w.m_accessoryClock.elapsed();
            if (!w.m_accessory.observe(role,AccessoryContract::PhysicalState::Flowing,"synthetic observation",now,now)) return false;
            w.publish(); return core.motionSafetyError().isEmpty();
        });
    }
    struct Barrier { QSemaphore entered,release; std::atomic_bool timedOut{false}; ~Barrier(){release.release();} };
    static auto hold(ControllerRuntimeSession &r) {
        auto state=std::make_shared<Barrier>();
        QMetaObject::invokeMethod(r.m_worker,[state]{state->entered.release();
            if (!state->release.tryAcquire(1,3000)) state->timedOut=true;},Qt::QueuedConnection);
        if (!state->entered.tryAcquire(1,3000)) return std::shared_ptr<Barrier>{};
        return state;
    }
    static bool beforeWrite(ControllerRuntimeSession &r,std::shared_ptr<std::atomic<qint64>> shift) {
        return run(r,[shift](ControllerRuntimeWorker &w) {
            w.m_watchdog->stop(); shift->store(1000000000LL);
            w.m_core->jog(.5,0); // Isolate the actual Core pre-write callback, not request drain.
            return !w.m_core->connected() && !w.m_core->positionReferenced;
        });
    }
    static bool clockUnavailable(ControllerRuntimeSession &r) {
        return run(r,[](ControllerRuntimeWorker &w) {
            w.m_clockSample=[] { return std::optional<ControllerRuntimeWorker::ClockInterval>{}; };
            return !w.checkClockContinuity() && !w.m_core->connected();
        });
    }
    static std::shared_ptr<std::atomic_bool> destroyed(ControllerRuntimeSession &r) {
        auto value=std::make_shared<std::atomic_bool>(false);
        if(!run(r,[value](ControllerRuntimeWorker &w){QObject::connect(&w,&QObject::destroyed,[value]{value->store(true);});return true;})) return {};
        return value;
    }
};
class RuntimeOwner {
  public:
    std::unique_ptr<ControllerRuntimeSession> runtime=std::make_unique<ControllerRuntimeSession>(DeviceProfile::m3Reference());
    std::shared_ptr<std::atomic_bool> ended=ControllerRuntimeTestAccess::destroyed(*runtime);
    ~RuntimeOwner() { runtime.reset(); QElapsedTimer timer;timer.start();
        while(ended && !ended->load() && timer.elapsed()<2500) { QCoreApplication::processEvents(); QThread::msleep(1); }
        if(!ended || !ended->load()) QTest::qFail("Worker teardown did not finish before owned peer EOF",__FILE__,__LINE__);
    }
};
class RuntimeContinuityTest : public QObject {
    Q_OBJECT
    const QString script=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/runtime_pty_peer.py");
    static PreparedControllerJob::Ptr job() {
        JobPlan plan; QPointF point(20,30);
        for(int i=0;i<2000;++i) { const QPointF next(i%2?20:70,30); plan.moves.append({point,next,5000,100,0,false});point=next; }
        return PreparedControllerJob::prepare(plan);
    }
    static QJsonArray events(RuntimePeer &peer) { const auto result=peer.snapshot();return result.ok?result.object.value("events").toArray():QJsonArray{}; }
    static qint64 resetAfter(const QJsonArray &journal,qint64 after) {
        for(const auto &v:journal){auto e=v.toObject();if(e["kind"]=="realtime"&&e["value"].toInt()==0x18&&e["atNs"].toInteger()>=after)return e["atNs"].toInteger();}return -1;
    }
    static int powered(const QJsonArray &journal) { int count=0;for(const auto &v:journal)if(v.toObject()["value"]=="M4 S1000")++count;return count; }
    static bool noUnsafeAfter(const QJsonArray &journal,qint64 after) {
        for(const auto &v:journal) {const auto e=v.toObject();if(e["atNs"].toInteger()<after)continue;
            if(e["kind"]=="realtime"&&e["value"].toInt()=='~')return false;
            if(e["kind"]!="command")continue;const auto s=e["value"].toString();
            if(s.startsWith("G0")||s.startsWith("G1")||s.startsWith("$J")||s.startsWith("M3")||s.startsWith("M4"))return false;
        }return true;
    }
    bool start(RuntimePeer &peer) {return peer.start(script).ok&&peer.control({{"action","set"},{"values",QJsonObject{{"runOnPoweredCommand",true}}}}).ok;}
    static bool clockFault(const ControllerRuntimeSession &r) {
        for(const auto &value:r.diagnosticSnapshot().value("events").toList())
            if(value.toMap().value("reason")=="hostClockContinuity") return r.status().state=="Verbindungsfehler";
        return false;
    }
  private slots:
    void idleFaultRevokesReferenceAndFlow_data() {
        QTest::addColumn<qint64>("delta");QTest::newRow("gap-increase")<<qint64(1000000000);
        QTest::newRow("gap-decrease")<<qint64(-1000000000);
    }
    void idleFaultRevokesReferenceAndFlow() {
        QFETCH(qint64,delta);
        RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(ControllerRuntimeTestAccess::commission(r));auto shift=ControllerRuntimeTestAccess::syntheticClock(r);QVERIFY(shift);
        const auto before=RuntimePeer::monotonicNs();shift->store(delta);
        QTRY_VERIFY(!r.status().connected);QVERIFY(clockFault(r));QVERIFY(!r.status().positionReferenced);
        QVERIFY(!r.status().extractionSafetyError.isEmpty());QVERIFY(!r.status().accessoryLeaseActive);
        const auto journal=events(peer);QVERIFY(resetAfter(journal,before)>=before);QVERIFY(noUnsafeAfter(journal,resetAfter(journal,before)));
        QVERIFY(!r.requestStart(job()).enqueued);QTest::qWait(100);QVERIFY(!r.status().connected);
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'z')).enqueued);
        QTRY_VERIFY(r.status().ready);QVERIFY(!r.status().positionReferenced);
        QVERIFY(!r.status().extractionSafetyError.isEmpty());
        const auto reconnected=RuntimePeer::monotonicNs();r.requestStart(job()); // Admission is not dispatch permission.
        QTest::qWait(100);QVERIFY(noUnsafeAfter(events(peer),reconnected));QVERIFY(!r.status().positionReferenced);
    }
    void pendingOpenTerminatesWithoutOpeningPort() {
        RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        auto shift=ControllerRuntimeTestAccess::syntheticClock(r);QVERIFY(shift);auto barrier=ControllerRuntimeTestAccess::hold(r);QVERIFY(barrier);
        const auto release=qScopeGuard([barrier]{barrier->release.release();});
        QSignalSpy result(&r,&ControllerSession::connectionRequestFinished);
        const auto submission=r.requestOpen(peer.port(),QByteArray(64,'b'));QVERIFY(submission.enqueued);
        shift->store(1000000000LL);barrier->release.release();QTRY_COMPARE(result.size(),1);
        QCOMPARE(result[0][0].toULongLong(),submission.requestId);QVERIFY(!result[0][1].toBool());
        QVERIFY(!r.status().connected);
        for(const auto &event:events(peer)) QCOMPARE(event.toObject().value("kind").toString(),QString("configured"));
        QCOMPARE(r.status().connectionEpoch,quint64(1));QVERIFY(!barrier->timedOut);
    }
    void queuedStartNeverReachesWire() {
        RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'c')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(ControllerRuntimeTestAccess::commission(r));auto shift=ControllerRuntimeTestAccess::syntheticClock(r);QVERIFY(shift);
        auto barrier=ControllerRuntimeTestAccess::hold(r);QVERIFY(barrier);const auto before=RuntimePeer::monotonicNs();
        const auto release=qScopeGuard([barrier]{barrier->release.release();});
        QVERIFY(ControllerRuntimeTestAccess::start(r,job()));shift->store(1000000000LL);barrier->release.release();
        QTRY_VERIFY(!r.status().connected);QCOMPARE(powered(events(peer)),0);QVERIFY(noUnsafeAfter(events(peer),before));
        QVERIFY(!barrier->timedOut);
    }
    void activeAndHeldJobStopDuringGuiBlock_data(){QTest::addColumn<bool>("hold");QTest::newRow("Run")<<false;QTest::newRow("Hold")<<true;}
    void activeAndHeldJobStopDuringGuiBlock() {
        QFETCH(bool,hold);RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'d')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(ControllerRuntimeTestAccess::commission(r));auto shift=ControllerRuntimeTestAccess::syntheticClock(r);QVERIFY(shift);
        QVERIFY(ControllerRuntimeTestAccess::start(r,job()));QTRY_VERIFY(powered(events(peer))>0);
        if(hold){r.pause();QTRY_VERIFY(r.status().paused);}
        const auto before=RuntimePeer::monotonicNs();shift->store(1000000000LL);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto returned=RuntimePeer::monotonicNs();const auto journal=events(peer);const auto reset=resetAfter(journal,before);
        QVERIFY(reset>=before&&reset<returned);QVERIFY(noUnsafeAfter(journal,reset));QVERIFY(!r.status().connected);
        QVERIFY(!r.status().accessoryLeaseActive);QVERIFY(!r.status().positionReferenced);QVERIFY(clockFault(r));
    }
    void prewriteChecksWithoutWatchdog() {
        RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'e')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(ControllerRuntimeTestAccess::commission(r));auto shift=ControllerRuntimeTestAccess::syntheticClock(r);QVERIFY(shift);
        const auto before=RuntimePeer::monotonicNs();QVERIFY(ControllerRuntimeTestAccess::beforeWrite(r,shift));
        QVERIFY(noUnsafeAfter(events(peer),before));QVERIFY(clockFault(r));
    }
    void unavailableClockFailsClosed() {
        RuntimePeer peer;QVERIFY(start(peer));RuntimeOwner owner;auto &r=*owner.runtime;
        QVERIFY(r.requestOpen(peer.port(),QByteArray(64,'f')).enqueued);QTRY_VERIFY(r.status().ready);
        QVERIFY(ControllerRuntimeTestAccess::commission(r));
        const auto before=RuntimePeer::monotonicNs();QVERIFY(ControllerRuntimeTestAccess::clockUnavailable(r));
        QTRY_VERIFY(resetAfter(events(peer),before)>=before);QVERIFY(clockFault(r));
        QVERIFY(!r.status().positionReferenced);QVERIFY(!r.status().extractionSafetyError.isEmpty());
    }
};
QTEST_MAIN(RuntimeContinuityTest)
#include "runtimecontinuity_test.moc"
