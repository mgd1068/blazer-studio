#include "nativeinputintent.h"
#include "machine.h"
#include "document.h"
#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "controllerruntimemailbox.h"
#include "support/runtimepeer.h"
#include <QJsonArray>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSemaphore>
#include <QTimer>
#include <QMutexLocker>
#include <QtTest>
#include <atomic>
#include <memory>

using RuntimeTestSupport::RuntimePeer;
class NativeInputIntentTestAccess {
public:
    static void capture(NativeInputIntentAdapter &a,QQuickWindow &w) {
        const auto n=NativeInputIntentAdapter::nowMs();
        a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        a.m_contextStarted=n-1000;a.m_sample={1000,n-5,n-4};
        a.capture(&w,1000,{10,10},false,true,false,false);
    }
};
class ControllerRuntimeTestAccess {
public:
    static ControllerRuntimeSession &runtime(Machine &m) {return *static_cast<ControllerRuntimeSession *>(m.session.get());}
    static NativeInputIntentAdapter &input(Machine &m) {return *m.nativeInput;}
    template<class Function> static bool run(Machine &m,Function fn) {
        auto worker=runtime(m).m_worker;
        struct State {QSemaphore done;std::atomic_bool cancelled{false};bool ok=false;};
        auto state=std::make_shared<State>();
        if(!QMetaObject::invokeMethod(worker,[worker,state,fn] {
            if(!state->cancelled.load() && worker->m_core)state->ok=fn(*worker);
            state->done.release();
        },Qt::QueuedConnection))return false;
        if(!state->done.tryAcquire(1,3000)){state->cancelled=true;return false;}
        return state->ok;
    }
    static bool commission(Machine &m) {
        return run(m,[](ControllerRuntimeWorker &w){
            auto &c=*w.m_core;if(!c.ready())return false;
            c.commissionedEnvelope=QRectF(0,0,200,150);c.commissionedModalContract=true;c.positionReferenced=true;
            w.m_accessory=AccessoryContract(true);
            constexpr auto role=AccessoryContract::Role::EnclosureExtraction;
            if(!w.m_accessory.configure(role,{"synthetic-only","offline test",true}))return false;
            const auto request=w.m_accessory.request(role,true);
            if(!request||!w.m_accessory.acknowledge(*request,true))return false;
            const auto now=w.m_accessoryClock.elapsed();
            if(!w.m_accessory.observe(role,AccessoryContract::PhysicalState::Flowing,"synthetic only",now,now))return false;
            w.publish();return true;
        });
    }
    struct Barrier {QSemaphore entered,release;~Barrier(){release.release();}};
    static std::shared_ptr<Barrier> hold(Machine &m) {
        auto b=std::make_shared<Barrier>();
        if(!QMetaObject::invokeMethod(runtime(m).m_worker,[b]{b->entered.release();b->release.tryAcquire(1,2000);},Qt::QueuedConnection)||!b->entered.tryAcquire(1,3000))return {};
        return b;
    }
    static std::shared_ptr<std::atomic_bool> destructionFence(Machine &m){
        auto done=std::make_shared<std::atomic_bool>(false);
        if(!run(m,[done](ControllerRuntimeWorker &w){QObject::connect(&w,&QObject::destroyed,[done]{*done=true;});return true;}))return {};
        return done;
    }
};
class NativeFocusRuntimeTest:public QObject {
    Q_OBJECT
    static QJsonArray events(RuntimePeer &peer){auto s=peer.snapshot();return s.object.value("events").toArray();}
    static int powered(const QJsonArray &events){int count=0;for(const auto &v:events){auto e=v.toObject();if(e["kind"]=="command"&&e["value"].toString().contains("M3 S"))++count;}return count;}
    struct Fixture {
        RuntimePeer peer;
        Document document;
        std::unique_ptr<Machine> machine;
        std::shared_ptr<std::atomic_bool> destroyed;
        QQuickWindow window;
        std::unique_ptr<QQuickItem> control;
        bool setup(){
            if(!peer.start(QString::fromUtf8(PEER_SCRIPT)).ok)return false;
            machine=std::make_unique<Machine>(&document);
            destroyed=ControllerRuntimeTestAccess::destructionFence(*machine);
            if(!destroyed)return false;
            window.resize(100,100);window.show();window.requestActivate();
            control=std::make_unique<QQuickItem>(window.contentItem());control->setSize({40,40});control->setProperty("pressed",true);
            machine->registerFocusControl(control.get());
            return machine->requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued;
        }
        void arm(){NativeInputIntentTestAccess::capture(ControllerRuntimeTestAccess::input(*machine),window);}
        ~Fixture(){
            machine.reset();
            QElapsedTimer clock;clock.start();
            while(destroyed&&!destroyed->load()&&clock.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
        }
    };
private slots:
    void actualMachineBridge_data(){
        QTest::addColumn<QString>("scenario");
        for(auto s:{"fresh","fake-qml","expired-before-qml","expired-in-worker","cancel-before-qml","cancel-before-worker"})QTest::newRow(s)<<QString(s);
    }
    void actualMachineBridge(){
        QFETCH(QString,scenario);Fixture f;QVERIFY(f.setup());
        QTRY_VERIFY_WITH_TIMEOUT(f.machine->ready(),5000);QTRY_VERIFY(f.window.isActive());
        QVERIFY(ControllerRuntimeTestAccess::commission(*f.machine));
        QTest::qWait(30); // Let actual ready/verified cache notifications settle before capture.
        f.arm();
        if(scenario=="fake-qml")ControllerRuntimeTestAccess::input(*f.machine).revoke();
        if(scenario=="expired-before-qml")QTest::qSleep(270);
        if(scenario=="cancel-before-qml")f.machine->endFocus();
        std::shared_ptr<ControllerRuntimeTestAccess::Barrier> barrier;
        if(scenario=="expired-in-worker"||scenario=="cancel-before-worker"){
            barrier=ControllerRuntimeTestAccess::hold(*f.machine);QVERIFY(barrier);
        }
        f.machine->beginFocus(); // Actual public bridge: consume -> original-time seal -> worker.
        if(scenario=="expired-in-worker")QTest::qSleep(270);
        if(scenario=="cancel-before-worker")f.machine->endFocus();
        if(barrier)barrier->release.release();
        if(scenario=="fresh"){
            QTRY_COMPARE_WITH_TIMEOUT(powered(events(f.peer)),1,3000);
            f.machine->endFocus();
            QTest::qWait(80);
            auto journal=events(f.peer);int first=-1,lastOff=-1;
            for(int i=0;i<journal.size();++i){auto e=journal[i].toObject();if(e["kind"]!="command")continue;auto s=e["value"].toString();if(s.contains("M3 S"))first=i;if(s=="M5 S0")lastOff=i;}
            QVERIFY(first>=0);QVERIFY(lastOff>first);QCOMPARE(powered(journal),1);
            QStringList commands;
            for(const auto &value:journal){const auto e=value.toObject();if(e["kind"]=="command")commands<<e["value"].toString();}
            const int poweredIndex=commands.indexOf("G1 F1 M3 S20");
            QVERIFY(poweredIndex>=3);
            QCOMPARE(commands.mid(poweredIndex-3,7),(QStringList{"M5","G21","G90","G1 F1 M3 S20","G4 P0.2","M5 S0","G0"}));
            for(const auto &command:commands){QVERIFY(command!="M8"&&command!="M9");QVERIFY(!command.contains(QRegularExpression("[XY][-+0-9.]")));}
            f.machine->beginFocus();QTest::qWait(50);QCOMPARE(powered(events(f.peer)),1); // One shot.
        }else{
            QTest::qWait(100);QCOMPARE(powered(events(f.peer)),0);
        }
    }
};
QTEST_MAIN(NativeFocusRuntimeTest)
#include "nativefocusruntime_test.moc"
