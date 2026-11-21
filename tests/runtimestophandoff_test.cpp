#include "controllerruntime.h"
#include "controllerruntimemailbox.h"
#include "controllerruntimeworker.h"
#include "document.h"
#include "machine.h"
#include "support/runtimepeer.h"
#include <QJsonArray>
#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;

class ControllerRuntimeTestAccess {
  public:
    struct Boundary {
        QSemaphore entered,release;
        bool connected=false,resetting=false,fault=false,rawStop=false,reserved=false,latestBusy=false;
    };
    static ControllerRuntimeSession &runtime(Machine &m){return *static_cast<ControllerRuntimeSession *>(m.session.get());}
    template<class Function>static bool run(Machine &m,Function fn){
        struct State{QSemaphore done;std::atomic_bool canceled{false};bool ok=false;};
        auto state=std::make_shared<State>();auto worker=runtime(m).m_worker;
        if(!worker||!QMetaObject::invokeMethod(worker,[state,worker,fn]{
            if(!state->canceled&&worker->m_core)state->ok=fn(*worker);state->done.release();
        },Qt::QueuedConnection))return false;
        if(!state->done.tryAcquire(1,3000)){state->canceled=true;return false;}return state->ok;
    }
    static std::shared_ptr<Boundary> installProbe(Machine &m){
        auto boundary=std::make_shared<Boundary>();
        if(!run(m,[boundary](ControllerRuntimeWorker &w){
            w.m_clockSample=[] {return std::optional<ControllerRuntimeWorker::ClockInterval>{{1000000000LL,1000000010LL}};};
            w.m_clockBaseline.reset();if(!w.checkClockContinuity())return false;
            auto worker=&w;
            w.m_stopHandoffProbe=[worker,boundary]{
                boundary->connected=worker->m_core->connected();boundary->resetting=worker->m_core->awaitingReset;
                boundary->fault=worker->m_core->state=="Verbindungsfehler";
                {QMutexLocker lock(&worker->m_mailbox->mutex);boundary->rawStop=worker->m_mailbox->stop.load();
                 boundary->reserved=worker->m_mailbox->dispatchInFlight;boundary->latestBusy=worker->m_mailbox->latest.busy;}
                boundary->entered.release();boundary->release.tryAcquire(1,3000);
            };
            return true;
        }))return {};return boundary;
    }
    static std::shared_ptr<Boundary> hold(Machine &m){
        auto boundary=std::make_shared<Boundary>();
        auto worker=runtime(m).m_worker;
        if(!QMetaObject::invokeMethod(worker,[boundary]{boundary->entered.release();boundary->release.tryAcquire(1,3000);},Qt::QueuedConnection)||
           !boundary->entered.tryAcquire(1,3000))return {};
        return boundary;
    }
    static std::shared_ptr<std::atomic_bool> destructionFence(Machine &m){
        auto value=std::make_shared<std::atomic_bool>(false);
        if(!run(m,[value](ControllerRuntimeWorker &w){QObject::connect(&w,&QObject::destroyed,[value]{value->store(true);});return true;}))return{};
        return value;
    }
};
class RuntimeStopHandoffTest:public QObject{
    Q_OBJECT
    struct Fixture {
        RuntimePeer peer;Document document;std::unique_ptr<Machine> machine;
        std::shared_ptr<std::atomic_bool> destroyed;
        bool setup(){
            if(!peer.start(QStringLiteral(PEER_SCRIPT)).ok)return false;
            machine=std::make_unique<Machine>(&document);destroyed=ControllerRuntimeTestAccess::destructionFence(*machine);
            return destroyed&&machine->requestConnectHardware(peer.port(),QByteArray(64,'s')).enqueued;
        }
        ~Fixture(){machine.reset();QElapsedTimer timer;timer.start();while(destroyed&&!destroyed->load()&&timer.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}}
    };
    static int resets(RuntimePeer &peer){const auto reply=peer.snapshot();int count=0;
        if(!reply.ok)return -1;for(const auto &v:reply.object.value("events").toArray()){auto e=v.toObject();if(e["kind"]=="realtime"&&e["value"].toInt()==0x18)++count;}return count;}
  private slots:
    void stopTransferNeverExposesIdleGuard(){
        Fixture f;QVERIFY(f.setup());auto &machine=*f.machine;QTRY_VERIFY(machine.ready());
        QVERIFY(f.document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(f.peer.control({{"action","set"},{"values",QJsonObject{{"resetEnabled",false}}}}).ok);
        const auto initial=resets(f.peer);QVERIFY(initial>=1);
        auto boundary=ControllerRuntimeTestAccess::installProbe(machine);QVERIFY(boundary);
        const auto releaseBoundary=qScopeGuard([boundary]{boundary->release.release(4);});
        auto hold=ControllerRuntimeTestAccess::hold(machine);QVERIFY(hold);
        const auto releaseHold=qScopeGuard([hold]{hold->release.release();});
        // Public data admission only: no native/motion grants. STOP discards
        // this queued Jog before any normal request can reach dispatch.
        ControllerRuntimeTestAccess::runtime(machine).jog(1,0);
        QVERIFY(machine.occupied());machine.stop();QVERIFY(machine.occupied());
        hold->release.release();QVERIFY(boundary->entered.tryAcquire(1,3000));
        const bool occupied=machine.occupied(),pending=machine.requestPending();
        const bool guarded=!f.document.synchronousWorkBlockReason().isEmpty();
        const auto atBoundary=resets(f.peer);
        qInfo()<<"STOP boundary connected/resetting/fault/stop/reserved/latestBusy/occupied/pending/guard/resets"
            <<boundary->connected<<boundary->resetting<<boundary->fault<<boundary->rawStop<<boundary->reserved
            <<boundary->latestBusy<<occupied<<pending<<guarded<<atBoundary;
        boundary->release.release();QTRY_COMPARE(resets(f.peer),initial+1);
        QVERIFY(boundary->connected);QVERIFY(!boundary->resetting);QVERIFY(!boundary->fault);
        QVERIFY(!boundary->rawStop);QVERIFY(!boundary->latestBusy);QCOMPARE(atBoundary,initial);
        QVERIFY(occupied);QVERIFY(pending);QVERIFY(guarded);
        QVERIFY(!f.document.synchronousWorkBlockReason().isEmpty()); // Reset has no automatic banner.
        auto reply=f.peer.snapshot();QVERIFY(reply.ok);
        for(const auto &v:reply.object.value("events").toArray()){auto e=v.toObject();if(e["kind"]=="command")QVERIFY(!e["value"].toString().startsWith("$J="));}
        QVERIFY(f.peer.control({{"action","reply"},{"data","Grbl 1.3a ['$' for help]\r\n"}}).ok);
        QTRY_VERIFY(machine.ready());QVERIFY(f.document.synchronousWorkBlockReason().isEmpty());
    }
};
QTEST_MAIN(RuntimeStopHandoffTest)
#include "runtimestophandoff_test.moc"
