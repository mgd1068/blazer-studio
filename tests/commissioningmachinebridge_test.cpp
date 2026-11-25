#include "machine.h"
#include "document.h"
#include "controllerruntime.h"
#include "controllerruntimeworker.h"
#include "nativeinputintent.h"
#include "support/runtimepeer.h"
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSemaphore>
#include <QtTest>
#include <QJsonArray>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;
// Private synthetic accepted activation, explicitly not an OS input assertion.
// Real registered QML controls and their real binding providers remain in use.
class NativeInputIntentTestAccess {
public:
    static bool seed(NativeInputIntentAdapter &a,QQuickItem &control,const QString &action,const QByteArray &binding) {
        const auto key=action+"#"+QString::number(quintptr(&control),16);
        if(binding.isEmpty()||a.m_controls.value(key)!=&control||!a.m_clickBindings.value(key))return false;
        const auto now=NativeInputIntentAdapter::nowMs();a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        a.m_sample={1000,now-5,now-4};
        auto token=std::make_shared<NativeInputContextToken>();a.m_tokens[key]=token;
        a.m_pending[key]={{++a.m_interaction,now,now+250,binding,token},&control,control.window(),a.m_context,false,true};
        return a.m_clickBindings.value(key)()==binding;
    }
};
class ControllerRuntimeTestAccess {
public:
    static ControllerRuntimeSession &runtime(Machine &m){return *static_cast<ControllerRuntimeSession *>(m.session.get());}
    static bool seed(Machine&m,QQuickItem&control,const QString&action,const QVariantMap&args={}){
        const auto binding=m.nativeBinding(action,args,m.session->status());
        const auto actual=m.nativeArguments(&control,{});
        if(m.nativeBinding(action,actual,m.session->status())!=binding)qWarning()<<"Registered QML arguments differ from typed facade"<<action<<args<<actual;
        return NativeInputIntentTestAccess::seed(*m.nativeInput,control,action,binding);
    }
};
namespace {
QQuickItem *find(QQuickItem *item,const QString &name){if(item->objectName()==name)return item;for(auto child:item->childItems())if(auto match=find(child,name))return match;return nullptr;}
}
class CommissioningMachineBridgeTest:public QObject {
    Q_OBJECT
    static int count(RuntimePeer &p,const QString &kind){return p.snapshot().object.value("counts").toObject().value(kind).toInt();}
private slots:
    void actualControlsMachineAndWorkerEndToEnd(){
        RuntimePeer peer;QVERIFY(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/m3_commissioning_peer.py")).ok);
        Document document;document.setBackgroundPlanning(false);document.newProject();document.addShape("rect");
        document.setProperty("x",5.);document.setProperty("y",5.);document.setProperty("width",5.);document.setProperty("height",5.);
        QVERIFY(document.setStartMode(Document::AbsoluteCoordinates));
        Machine machine(&document);QVERIFY(machine.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(machine.ready());QTRY_VERIFY(machine.jobPrepared());
        QQmlEngine engine;engine.rootContext()->setContextProperty("actualMachine",&machine);
        QString qmlDir=qEnvironmentVariable("BLAZER_REAL_COMMISSIONING_QML_DIR");if(qmlDir.isEmpty())qmlDir=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        component.setData(QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {
 width:1200; height:1000; visible:true
 property alias setup: setup
 App.M3CommissioningDialog { id:setup; modelMachine:actualMachine }
 App.ExtractionSupervisionPanel { id:supervision; anchors.fill:parent; modelMachine:actualMachine; commissioningComplete:actualMachine.commissioning.stage === "complete" }
 Button { id:start; objectName:"bridgeStart"; x:900; y:850; text:"Start"; property bool nativeActionEnabled:true; Component.onCompleted:actualMachine.registerNativeActionControl(start,"start"); onClicked:actualMachine.start() }
 Button { id:resume; objectName:"bridgeResume"; x:1000; y:850; text:"Resume"; property bool nativeActionEnabled:true; Component.onCompleted:actualMachine.registerNativeActionControl(resume,"resume"); onClicked:actualMachine.pause() }
})").arg(QUrl::fromLocalFile(qmlDir).toString()).toUtf8(),QUrl());
        std::unique_ptr<QObject> root(component.create());QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow *>(root.get());QVERIFY(window);window->requestActivate();QVERIFY(QTest::qWaitForWindowActive(window,1000));
        auto item=[&](const QString&name){return find(window->contentItem(),name);};
        auto invoke=[&](const QString&name){QCoreApplication::processEvents();auto control=item(name);return control&&control->isEnabled()&&QMetaObject::invokeMethod(control,"clicked");};
        auto check=[&](const QString&name){QCoreApplication::processEvents();auto control=item(name);return control&&control->setProperty("checked",true);};
        auto native=[&](const QString&name,const QString&action,const QVariantMap&args=QVariantMap{}){auto control=item(name);return control&&control->isEnabled()&&ControllerRuntimeTestAccess::seed(machine,*control,action,args)&&QMetaObject::invokeMethod(control,"clicked");};
        auto stage=[&]{auto setup=root->property("setup").value<QObject*>();return setup?setup->property("stage").toString():QString();};
        auto setup=root->property("setup").value<QObject*>();QVERIFY(setup);QVERIFY(QMetaObject::invokeMethod(setup,"open"));QTRY_COMPARE(stage(),QString("mechanics"));
        for(const auto name:{"setupSwitchesWitness","setupDirectionWitness","setupFreeTravelWitness","setupLaserDisabledWitness"})QVERIFY(check(name));
        QVERIFY(invoke("attestSetupMechanics"));QTRY_COMPARE(stage(),QString("homeReady"));
        QSignalSpy denied(&machine,&Machine::nativeActionDenied);
        const int homesBefore=count(peer,"homed");
        machine.commissioningHome(); // No native activation: reject, explain, never send $H.
        QTRY_COMPARE(denied.count(),1);
        QCOMPARE(denied.first().at(0).toString(),QString("setupHome"));
        auto rejection=item("commissioningActionError");QVERIFY(rejection);
        QTRY_VERIFY(rejection->isVisible());
        QCOMPARE(rejection->property("text").toString(),denied.first().at(1).toString());
        QCOMPARE(count(peer,"homed"),homesBefore);
        QCOMPARE(stage(),QString("homeReady"));
        QTRY_VERIFY(!machine.requestPending());
        QVERIFY(native("setupHomeButton","setupHome"));QTRY_COMPARE(stage(),QString("geometry"));
        QTRY_VERIFY(!rejection->isVisible());
        QVERIFY(check("setupObservedHome"));QVERIFY(invoke("confirmSetupHome"));QTRY_VERIFY(machine.commissioning().value("homeObserved").toBool());
        QVERIFY(native("setupProbe_1_0","setupProbe",{{"dx",1.},{"dy",0.}}));QTRY_COMPARE(stage(),QString("probeConfirm"));
        QVERIFY(check("setupMeasuredProbe"));QVERIFY(invoke("confirmSetupProbe"));QTRY_COMPARE(stage(),QString("geometry"));
        QVERIFY(native("setupProbe_0_1","setupProbe",{{"dx",0.},{"dy",1.}}));QTRY_COMPARE(stage(),QString("probeConfirm"));
        QVERIFY(check("setupMeasuredProbe"));QVERIFY(invoke("confirmSetupProbe"));QTRY_COMPARE(stage(),QString("limits"));
        QVERIFY(item("setupMinX")->setProperty("text","-190"));QVERIFY(item("setupMinY")->setProperty("text","-140"));QVERIFY(item("setupMaxX")->setProperty("text","-10"));QVERIFY(item("setupMaxY")->setProperty("text","-10"));
        QVERIFY(invoke("submitSetupLimits"));QTRY_COMPARE(stage(),QString("cornerReady"));
        for(int i=0;i<4;++i){QVERIFY(native("setupCornerButton","setupCorner",{{"index",i}}));QTRY_COMPARE(stage(),QString("cornerConfirm"));QVERIFY(check("setupObservedCorner"));QVERIFY(invoke("confirmSetupCorner"));QTRY_COMPARE(stage(),i==3?QString("workCoordinates"):QString("cornerReady"));}
        QVERIFY(native("setupZeroButton","setupZero"));QTRY_VERIFY(std::abs(machine.x())<.001);QTRY_COMPARE(stage(),QString("workCoordinates"));
        QVERIFY(check("setupCoordinatesWitness"));QVERIFY(invoke("confirmSetupCoordinates"));QTRY_COMPARE(stage(),QString("complete"));QVERIFY(invoke("closeSetup"));QTRY_VERIFY(!setup->property("visible").toBool());
        QVERIFY(check("extractionClearanceWitness"));QVERIFY(invoke("confirmExtractionClearance"));QTRY_VERIFY(machine.extractionSupervision().value("reason").toString().startsWith("Sicheren Zustand"));
        QVERIFY(invoke("selectAttendedExtraction"));QTRY_COMPARE(machine.extractionSupervision().value("mode").toString(),QString("operatorAttendedExternal"));
        auto attest=[&](){return check("externalExtractionOnWitness")&&check("operatorPresentWitness")&&check("attendedWorkAreaWitness")&&invoke("attestExternalExtraction");};
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("start"));
        QVERIFY(native("bridgeStart","start"));QTRY_VERIFY(count(peer,"powered")>0);QTRY_VERIFY(!machine.occupied());
        QCOMPARE(count(peer,"collision"),0);QVERIFY(machine.extractionSupervision().value("pendingAction").toString().isEmpty());
        const auto before=count(peer,"powered");machine.start();QTest::qWait(80);QCOMPARE(count(peer,"powered"),before); // No new native activation/witness.
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("start"));
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);
        QVERIFY(native("bridgeStart","start"));QTRY_VERIFY(machine.busy());machine.pause();QTRY_VERIFY(machine.holdConfirmed());
        QTRY_COMPARE(item("extractionActionChoice")->property("currentValue").toString(),QString("resume"));
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("resume"));
        QVERIFY(native("bridgeResume","resume"));QTRY_VERIFY(!machine.paused());
        bool resumed=false;for(const auto &entry:peer.snapshot().object.value("events").toArray()){const auto e=entry.toObject();if(e.value("kind")=="realtime"&&e.value("value").toInt()==int('~'))resumed=true;}QVERIFY(resumed);
        QVERIFY(invoke("revokeExternalAttendance"));QTRY_COMPARE(stage(),QString("fault"));QVERIFY(!machine.positionReferenced());QVERIFY(!machine.motionSafetyError().isEmpty());
        machine.useSimulator();QTRY_VERIFY(machine.simulation()&&machine.connectionPhase()=="closed");
    }
};
QTEST_MAIN(CommissioningMachineBridgeTest)
#include "commissioningmachinebridge_test.moc"
