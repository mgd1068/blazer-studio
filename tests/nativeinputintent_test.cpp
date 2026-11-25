#include "nativeinputintent.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>
#include <time.h>

class TestAcceptedButton final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool pressed READ pressed WRITE setPressed NOTIFY pressedChanged)
    Q_PROPERTY(bool nativeActionEnabled READ eligible WRITE setEligible NOTIFY eligibilityChanged)
  public:
    explicit TestAcceptedButton(QQuickItem *parent):QQuickItem(parent){setSize({30,30});}
    bool pressed()const{return m_pressed;} bool eligible()const{return m_eligible;}
    void setPressed(bool v){if(v!=m_pressed){m_pressed=v;emit pressedChanged();}}
    void setEligible(bool v){if(v!=m_eligible){m_eligible=v;emit eligibilityChanged();}}
  signals:void pressedChanged();void eligibilityChanged();
  private:bool m_pressed=false,m_eligible=true;
};

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
    static quint32 current(NativeInputIntentAdapter &a) {
        const auto n=now();a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        a.sample(quint32(n),n-1,n);return quint32(n);
    }
    static void release(NativeInputIntentAdapter &a,QQuickWindow &w,quint64 time) {
        a.release(&w,time,{10,10},false,true,false,false,0,nullptr);
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
    void focusReleaseDoesNotRevokeCommittedWindowContext() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QSignalSpy holds(&adapter,&NativeInputIntentAdapter::invalidated);
        QSignalSpy contexts(&adapter,&NativeInputIntentAdapter::contextInvalidated);
        QQuickItem focus;
        adapter.registerControl(&focus,"focus");
        contexts.clear();holds.clear();
        focus.setEnabled(false);
        QVERIFY(holds.count()>0);QCOMPARE(contexts.count(),0);
        adapter.revokeFocus();QCOMPARE(contexts.count(),0);
        adapter.revokeActionIntents();QCOMPARE(contexts.count(),0);
        adapter.revoke();QCOMPARE(contexts.count(),1);
    }
    void globalContextRevocationAllowsSynchronousDeletion() {
        auto *adapter=new NativeInputIntentAdapter(nullptr,false);
        QPointer<NativeInputIntentAdapter> alive(adapter);
        connect(adapter,&NativeInputIntentAdapter::contextInvalidated,adapter,[adapter]{delete adapter;});
        adapter->revoke();QVERIFY(!alive);
    }
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
        QTest::qWait(20); // The synthetic native press must be newer than control registration.
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
    void acceptedLongHoldUsesFreshRelease() {
        NativeInputIntentAdapter a(nullptr,false);QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();QTRY_VERIFY(window.isActive());
        TestAcceptedButton button(window.contentItem());a.registerClickControl(&button,"start#one",[]{return QByteArray("source-A");});QTest::qWait(20);
        NativeInputIntentTestAccess::capture(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(true);
        QTest::qSleep(350); // Press deadline expired, but an already-accepted bounded hold is legitimate.
        NativeInputIntentTestAccess::release(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(false);
        const auto intent=a.consumeMatching("start","source-A");QVERIFY(intent);QVERIFY(intent->contextToken&&intent->contextToken->valid);
        QVERIFY(intent->expiresAtMs-NativeInputIntentTestAccess::now()<=250);QVERIFY(!a.consumeMatching("start","source-A"));
    }
    void oldOrUnacceptedPressCannotRevive_data() {
        QTest::addColumn<bool>("old");QTest::newRow("old-press")<<true;QTest::newRow("unaccepted-press")<<false;
    }
    void oldOrUnacceptedPressCannotRevive() {
        QFETCH(bool,old);NativeInputIntentAdapter a(nullptr,false);QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();QTRY_VERIFY(window.isActive());
        TestAcceptedButton button(window.contentItem());a.registerClickControl(&button,"start#one",[]{return QByteArray("A");});QTest::qWait(20);
        auto stamp=NativeInputIntentTestAccess::current(a);NativeInputIntentTestAccess::capture(a,window,old?quint32(stamp-400):stamp);
        if(old)button.setPressed(true); // Fresh release cannot arm a rejected old press.
        NativeInputIntentTestAccess::release(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(false);
        QVERIFY(!a.consumeMatching("start","A"));
    }
    void eligibilityAndArgumentsCannotChangeBetweenPhases() {
        NativeInputIntentAdapter a(nullptr,false);QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();QTRY_VERIFY(window.isActive());
        TestAcceptedButton button(window.contentItem());QByteArray binding="A";a.registerClickControl(&button,"jog#one",[&]{return binding;});QTest::qWait(20);
        NativeInputIntentTestAccess::capture(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(true);binding="B";
        NativeInputIntentTestAccess::release(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(false);QVERIFY(!a.consumeMatching("jog","B"));
        button.setEligible(false);QTest::qWait(20);NativeInputIntentTestAccess::capture(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(true);
        button.setEligible(true);NativeInputIntentTestAccess::release(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(false);QVERIFY(!a.consumeMatching("jog","B"));
    }
    void enablingBusyStateDoesNotRevokeSubmittedButContextDoes() {
        NativeInputIntentAdapter a(nullptr,false);QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();QTRY_VERIFY(window.isActive());
        TestAcceptedButton button(window.contentItem());a.registerClickControl(&button,"start#one",[]{return QByteArray("A");});QTest::qWait(20);
        NativeInputIntentTestAccess::capture(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(true);
        NativeInputIntentTestAccess::release(a,window,NativeInputIntentTestAccess::current(a));button.setPressed(false);
        auto intent=a.consumeMatching("start","A");QVERIFY(intent);
        button.setEnabled(false);QVERIFY(intent->contextToken->valid);a.revokeFocus();QVERIFY(intent->contextToken->valid);
        a.revoke();QVERIFY(!intent->contextToken->valid);
    }
};
QTEST_MAIN(NativeInputIntentTest)
#include "nativeinputintent_test.moc"
