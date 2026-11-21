#include "nativeinputintent.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>
#include <time.h>

class NativeInputIntentTestAccess {
  public:
    static void x11(NativeInputIntentAdapter &a, quint32 server, qint64 before, qint64 after) {
        a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        a.m_sample={};
        a.m_contextStarted=before-1000;
        a.sample(server,before,after);
    }
    static auto map(NativeInputIntentAdapter &a, quint64 value, qint64 now) { return a.mapTimestamp(value,now); }
    static qint64 now() { return NativeInputIntentAdapter::nowMs(); }
    static void kwin(NativeInputIntentAdapter &a) {
        a.m_domain=NativeInputIntentAdapter::Domain::KWinMonotonic32;
        a.m_contextStarted=now()-1000;
    }
    static void capture(NativeInputIntentAdapter &a,QQuickWindow &w,quint64 time,
                        bool spontaneous=true,bool repeat=false,bool synth=false) {
        a.capture(&w,time,{10,10},false,spontaneous,repeat,synth);
    }
    static void forceSuspend(NativeInputIntentAdapter &a) {
        a.m_suspendGap+=1000; // Nonnegative baseline discontinuity, even on never-suspended hosts.
    }
    static bool inactive(const NativeInputIntentAdapter &a) {
        return !a.m_probeTimer.isActive() && !a.m_xcb && !a.m_checkPending && a.m_compositorOwner.isEmpty();
    }
};
class NativeInputIntentTest : public QObject {
    Q_OBJECT
  private slots:
    void disabledDoesNotRegisterPlatformWork() {
        NativeInputIntentAdapter a(nullptr,false);
        QVERIFY(NativeInputIntentTestAccess::inactive(a));
        QTest::qWait(220);
        QVERIFY(NativeInputIntentTestAccess::inactive(a));
        QVERIFY(!a.consume("focus"));
    }
    void unknownIsClosed() {
        NativeInputIntentAdapter a;
        QVERIFY(!a.reason().isEmpty());
        QVERIFY(!NativeInputIntentTestAccess::map(a,100,100));
        QVERIFY(!a.consume("focus"));
    }
    void x11ConservativeAgeAndWrap() {
        NativeInputIntentAdapter a;
        NativeInputIntentTestAccess::x11(a,1000,10000,10010);
        const auto proof=NativeInputIntentTestAccess::map(a,1020,10030);
        QVERIFY(proof);
        QCOMPARE(proof->observedAtMs,qint64(10013)); // Upper age, not arrival/midpoint.
        QCOMPARE(proof->expiresAtMs,qint64(10263));
        QVERIFY(!NativeInputIntentTestAccess::map(a,700,10030));
        QVERIFY(!NativeInputIntentTestAccess::map(a,2000,10030));
        QVERIFY(!NativeInputIntentTestAccess::map(a,0,10030));
        QVERIFY(!NativeInputIntentTestAccess::map(a,quint64(1)<<32,10030));
        QVERIFY(!NativeInputIntentTestAccess::map(a,1020,10600));
        NativeInputIntentTestAccess::x11(a,0xfffffff0u,20000,20001);
        const auto wrapped=NativeInputIntentTestAccess::map(a,0x10u,20040);
        QVERIFY(wrapped);
        QCOMPARE(wrapped->observedAtMs,qint64(20025));
    }
    void wideOrBackwardSampleDoesNotCalibrate() {
        NativeInputIntentAdapter a;
        NativeInputIntentTestAccess::x11(a,1000,10000,10031);
        QVERIFY(!NativeInputIntentTestAccess::map(a,1000,10032));
        NativeInputIntentTestAccess::x11(a,1000,10000,9999);
        QVERIFY(!NativeInputIntentTestAccess::map(a,1000,10001));
    }
    void kwinUsesOriginalMonotonicTimestamp() {
        NativeInputIntentAdapter a;
        NativeInputIntentTestAccess::kwin(a);
        timespec t{}; QVERIFY(clock_gettime(CLOCK_MONOTONIC,&t)==0);
        auto stamp=quint32(t.tv_sec*1000LL+t.tv_nsec/1000000);
        auto proof=NativeInputIntentTestAccess::map(a,stamp,NativeInputIntentTestAccess::now());
        QVERIFY(proof);
        QVERIFY(proof->expiresAtMs-NativeInputIntentTestAccess::now()<=248);
        QTest::qSleep(270); // Delayed delivery must not acquire a new 250ms budget.
        QVERIFY(!NativeInputIntentTestAccess::map(a,stamp,NativeInputIntentTestAccess::now()));
        QVERIFY(!NativeInputIntentTestAccess::map(a,stamp+10000,NativeInputIntentTestAccess::now()));
    }
    void oneShotContextAndSyntheticDeny() {
        NativeInputIntentAdapter a;
        QQuickWindow window; window.resize(100,100); window.show(); window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QQuickItem control(window.contentItem()); control.setSize({30,30});
        control.setProperty("pressed",true); // Private fixture models actual Button acceptance.
        a.registerControl(&control,"focus");
        auto arm=[&] {
            const auto n=NativeInputIntentTestAccess::now();
            NativeInputIntentTestAccess::x11(a,1000,n-5,n-4);
        };
        arm(); NativeInputIntentTestAccess::capture(a,window,1000,false);
        QVERIFY(!a.consume("focus"));
        arm(); NativeInputIntentTestAccess::capture(a,window,1000,true,true);
        QVERIFY(!a.consume("focus"));
        arm(); NativeInputIntentTestAccess::capture(a,window,1000,true,false,true);
        QVERIFY(!a.consume("focus"));
        control.setProperty("pressed",false);
        arm(); NativeInputIntentTestAccess::capture(a,window,1000);
        QVERIFY(!a.consume("focus"));
        control.setProperty("pressed",true);
        arm(); NativeInputIntentTestAccess::capture(a,window,1000);
        QVERIFY(a.consume("focus")); QVERIFY(!a.consume("focus"));
        arm(); NativeInputIntentTestAccess::capture(a,window,1000);
        QTest::qSleep(270);
        QVERIFY(!a.consume("focus"));
        arm(); NativeInputIntentTestAccess::capture(a,window,1000);
        a.revoke(); QVERIFY(!a.consume("focus"));
        arm(); NativeInputIntentTestAccess::capture(a,window,1000);
        control.setEnabled(false); QVERIFY(!a.consume("focus"));
    }
    void suspendAndDirectDeleteReentrancy() {
        auto a=new NativeInputIntentAdapter;
        NativeInputIntentTestAccess::kwin(*a);
        NativeInputIntentTestAccess::forceSuspend(*a);
        connect(a,&NativeInputIntentAdapter::invalidated,a,[a]{delete a;});
        QVERIFY(!NativeInputIntentTestAccess::map(*a,1000,NativeInputIntentTestAccess::now()));
    }
};
QTEST_MAIN(NativeInputIntentTest)
#include "nativeinputintent_test.moc"
