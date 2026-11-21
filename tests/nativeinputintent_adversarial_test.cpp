#include "nativeinputintent.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QtTest>

// Private seam, synthetic timestamps/events only. No claim of physical input.
class NativeInputIntentTestAccess {
public:
    static qint64 now() { return NativeInputIntentAdapter::nowMs(); }
    static void calibrate(NativeInputIntentAdapter &a, quint32 server=1000) {
        a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        qint64 birth=-1;for(const auto cutoff:a.m_actionCutoff)birth=std::max(birth,cutoff);
        const auto elapsed=now()-birth;
        if(elapsed<21)QTest::qWait(int(21-elapsed));
        // The fixture's conservative event time is n-12. Keep it newer than
        // actual control registration instead of backdating a new control's press.
        const auto n=now(); a.m_contextStarted=n-1000; a.m_sample={server,n-5,n-4};
    }
    static auto map(NativeInputIntentAdapter &a,quint64 t,qint64 now) {return a.mapTimestamp(t,now);}
    static void capture(NativeInputIntentAdapter &a,QQuickWindow &w,bool key=false,
                        bool spontaneous=true,bool repeat=false,bool synthetic=false) {
        a.capture(&w,1000,{10,10},key,spontaneous,repeat,synthetic);
    }
    static void deliver(NativeInputIntentAdapter &a,QObject *w,QEvent *e) {a.eventFilter(w,e);}
    static void fixture(NativeInputIntentAdapter &a,quint32 server,qint64 before,qint64 after) {
        a.m_domain=NativeInputIntentAdapter::Domain::X11Server32; a.m_contextStarted=before-1000;a.m_sample={server,before,after};
    }
    static void suspendedSinceCapture(NativeInputIntentAdapter &a) {a.m_suspendGap+=1000;}
};
class NativeInputAdversarialTest:public QObject {
    Q_OBJECT
private slots:
    void x11IndependentTimeBoundaries_data() {
        QTest::addColumn<quint32>("server");QTest::addColumn<quint64>("event");
        QTest::addColumn<qint64>("now");QTest::addColumn<qint64>("expectedObserved");
        QTest::newRow("ordinary")<<quint32(1000)<<quint64(1000)<<qint64(10010)<<qint64(9993);
        QTest::newRow("exact250ms")<<quint32(1000)<<quint64(767)<<qint64(10010)<<qint64(9760);
        QTest::newRow("older251ms")<<quint32(1000)<<quint64(766)<<qint64(10010)<<qint64(-1);
        QTest::newRow("future")<<quint32(1000)<<quint64(1011)<<qint64(10010)<<qint64(-1);
        QTest::newRow("afterWrap")<<quint32(0xfffffff0)<<quint64(0x10)<<qint64(10040)<<qint64(10025);
        QTest::newRow("beforeWrap")<<quint32(0x10)<<quint64(0xfffffff0)<<qint64(10010)<<qint64(9961);
        QTest::newRow("calibrationTooOld")<<quint32(1000)<<quint64(1500)<<qint64(10502)<<qint64(-1);
        QTest::newRow("zero")<<quint32(1000)<<quint64(0)<<qint64(10010)<<qint64(-1);
        QTest::newRow("not32bit")<<quint32(1000)<<quint64(1ULL<<32)<<qint64(10010)<<qint64(-1);
        QTest::newRow("ambiguousHalfWrap")<<quint32(1000)<<quint64(1000ULL+(1ULL<<31))<<qint64(10010)<<qint64(-1);
    }
    void x11IndependentTimeBoundaries() {
        QFETCH(quint32,server);QFETCH(quint64,event);QFETCH(qint64,now);QFETCH(qint64,expectedObserved);
        NativeInputIntentAdapter adapter(nullptr,false);
        NativeInputIntentTestAccess::fixture(adapter,server,10000,10001);
        const auto proof=NativeInputIntentTestAccess::map(adapter,event,now);
        QCOMPARE(bool(proof),expectedObserved>=0);
        if(proof){QCOMPARE(proof->observedAtMs,expectedObserved);QCOMPARE(proof->expiresAtMs,expectedObserved+250);}
    }
    void pendingCaptureCannotSurviveClockDiscontinuity() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        adapter.registerControl(&control,"focus");
        NativeInputIntentTestAccess::calibrate(adapter);
        NativeInputIntentTestAccess::capture(adapter,window);
        NativeInputIntentTestAccess::suspendedSinceCapture(adapter);
        // Keep the baseline valid/nonnegative on never-suspended hosts. A
        // deliberately inconsistent BOOTTIME-MONOTONIC baseline must be
        // rechecked at consume, without another press/map call.
        QVERIFY(!adapter.consume("focus"));
    }
    void keyboardProofCannotFollowControlFocusLoss() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QQuickItem control(window.contentItem()),other(window.contentItem());
        control.setSize({40,40});control.setProperty("pressed",true);other.setPosition({60,0});other.setSize({30,30});
        adapter.registerControl(&control,"focus");control.forceActiveFocus();
        QVERIFY(control.hasActiveFocus());
        NativeInputIntentTestAccess::calibrate(adapter);
        NativeInputIntentTestAccess::capture(adapter,window,true);
        other.forceActiveFocus();QVERIFY(!control.hasActiveFocus());
        QVERIFY(!adapter.consume("focus"));
    }
    void releaseBeforeConsumptionInvalidates() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        adapter.registerControl(&control,"focus");
        NativeInputIntentTestAccess::calibrate(adapter);
        NativeInputIntentTestAccess::capture(adapter,window);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(10,10),QPointF(10,10),
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        NativeInputIntentTestAccess::deliver(adapter,&window,&release);
        QVERIFY(!adapter.consume("focus"));
    }
    void delayedConsumptionRetainsOriginalDeadline() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        adapter.registerControl(&control,"focus");NativeInputIntentTestAccess::calibrate(adapter);
        const auto before=NativeInputIntentTestAccess::now();NativeInputIntentTestAccess::capture(adapter,window);
        QTest::qSleep(80);auto proof=adapter.consume("focus");QVERIFY(proof);
        QVERIFY(proof->observedAtMs<=before);QVERIFY(proof->expiresAtMs<=before+250);
        QVERIFY(proof->expiresAtMs-NativeInputIntentTestAccess::now()<175);
        NativeInputIntentTestAccess::calibrate(adapter);NativeInputIntentTestAccess::capture(adapter,window);
        QTest::qSleep(260);QVERIFY(!adapter.consume("focus"));
    }
    void registrationWindowVisibilityAndDestroyedControlRevoke() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        QQuickItem other(window.contentItem());other.setPosition({60,0});other.setSize({30,30});
        adapter.registerControl(&control,"focus");
        auto capture=[&]{NativeInputIntentTestAccess::calibrate(adapter);NativeInputIntentTestAccess::capture(adapter,window);};
        capture();adapter.registerControl(&other,"focus");QVERIFY(!adapter.consume("focus"));
        adapter.registerControl(&control,"focus");capture();control.setVisible(false);QVERIFY(!adapter.consume("focus"));
        control.setVisible(true);capture();control.setEnabled(false);QVERIFY(!adapter.consume("focus"));
        control.setEnabled(true);capture();QQuickWindow otherWindow;control.setParentItem(otherWindow.contentItem());
        QVERIFY(!adapter.consume("focus"));control.setParentItem(window.contentItem());
        auto transient=new QQuickItem(window.contentItem());transient->setSize({40,40});
        adapter.registerControl(transient,"focus");capture();delete transient;QVERIFY(!adapter.consume("focus"));
    }
    void continuityRevocationCanDestroyCollectorDirectly() {
        auto adapter=new NativeInputIntentAdapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        adapter->registerControl(&control,"focus");NativeInputIntentTestAccess::calibrate(*adapter);
        NativeInputIntentTestAccess::capture(*adapter,window);
        NativeInputIntentTestAccess::suspendedSinceCapture(*adapter);
        QPointer<NativeInputIntentAdapter> alive(adapter);
        connect(adapter,&NativeInputIntentAdapter::invalidated,adapter,[adapter]{delete adapter;});
        QVERIFY(!adapter->consume("focus"));QVERIFY(!alive);
    }
    void actualControlAcceptanceAndOcclusionRequired() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());QQuickItem control(window.contentItem());control.setSize({40,40});
        adapter.registerControl(&control,"focus");
        auto capture=[&]{NativeInputIntentTestAccess::calibrate(adapter);NativeInputIntentTestAccess::capture(adapter,window);};
        capture();QVERIFY(!adapter.consume("focus")); // No readable bool pressed property.
        control.setProperty("pressed",false);capture();QVERIFY(!adapter.consume("focus"));
        control.setProperty("pressed",QString("true"));capture();QVERIFY(!adapter.consume("focus"));
        control.setProperty("pressed",true);
        QQuickItem overlay(window.contentItem());overlay.setSize({40,40});overlay.setZ(10);
        capture();QVERIFY(!adapter.consume("focus")); // Registered item is behind another item.
        overlay.setVisible(false);capture();QVERIFY(adapter.consume("focus"));
        capture();control.setProperty("pressed",false);QVERIFY(!adapter.consume("focus"));
    }
    void fakeAndRepeatedSignalsCannotMint() {
        NativeInputIntentAdapter adapter(nullptr,false);
        QQuickWindow window;window.resize(100,100);window.show();window.requestActivate();
        QTRY_VERIFY(window.isActive());
        QQuickItem control(window.contentItem());control.setSize({40,40});control.setProperty("pressed",true);
        adapter.registerControl(&control,"focus");
        NativeInputIntentTestAccess::calibrate(adapter);
        NativeInputIntentTestAccess::capture(adapter,window,false,false);
        QVERIFY(!adapter.consume("focus"));
        NativeInputIntentTestAccess::capture(adapter,window,false,true,true);
        QVERIFY(!adapter.consume("focus"));
        NativeInputIntentTestAccess::capture(adapter,window,false,true,false,true);
        QVERIFY(!adapter.consume("focus"));
        NativeInputIntentTestAccess::capture(adapter,window);
        auto proof=adapter.consume("focus");QVERIFY(proof);
        QVERIFY(proof->expiresAtMs<=NativeInputIntentTestAccess::now()+238);
        QVERIFY(!adapter.consume("focus"));
    }
};
QTEST_MAIN(NativeInputAdversarialTest)
#include "nativeinputintent_adversarial_test.moc"
