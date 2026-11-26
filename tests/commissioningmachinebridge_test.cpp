#include "machine.h"
#include "applicationservices.h"
#include "materiallibrary.h"
#include "registration.h"
#include "workspace.h"
#include "reliefpreview.h"
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
#include <QTemporaryDir>
#include <QFile>
#include <atomic>
using RuntimeTestSupport::RuntimePeer;
// Private synthetic accepted activation, explicitly not an OS input assertion.
// Real registered QML controls and their real binding providers remain in use.
class NativeInputIntentTestAccess {
public:
    static bool hit(QQuickWindow *window,QQuickItem *control) {
        if(!control)return false;
        const auto scene=control->mapToScene(control->boundingRect().center());
        const auto okay=NativeInputIntentAdapter::hitControl(window,control,scene,false);
        return okay;
    }
    static bool seed(NativeInputIntentAdapter &a,QQuickItem &control,const QString &action,const QByteArray &binding) {
        const auto key=action+"#"+QString::number(quintptr(&control),16);
        if(binding.isEmpty()||a.m_controls.value(key)!=&control||!a.m_clickBindings.value(key)){qWarning()<<"Missing native binding/control"<<key<<binding.size()<<a.m_controls.size()<<a.m_controls.keys()<<control.property("nativeActionRole");return false;}
        const auto now=NativeInputIntentAdapter::nowMs();a.m_domain=NativeInputIntentAdapter::Domain::X11Server32;
        a.m_sample={1000,now-5,now-4};
        auto token=std::make_shared<NativeInputContextToken>();a.m_tokens[key]=token;
        a.m_pending[key]={{++a.m_interaction,now,now+250,binding,token},&control,control.window(),a.m_context,false,true};
        return a.m_clickBindings.value(key)()==binding;
    }
};
class ControllerRuntimeTestAccess {
public:
    struct Gate { QSemaphore entered, release; };
    static bool holdWorker(Machine &m, const std::shared_ptr<Gate> &gate) {
        auto &r=runtime(m);
        QMetaObject::invokeMethod(r.m_worker,[gate]{gate->entered.release();gate->release.tryAcquire(1,3000);},Qt::QueuedConnection);
        return gate->entered.tryAcquire(1,1000);
    }
    static ControllerRuntimeSession &runtime(Machine &m){return *static_cast<ControllerRuntimeSession *>(m.session.get());}
    static QString workerState(Machine &m) {
        auto *worker=runtime(m).m_worker;QString result;
        QMetaObject::invokeMethod(worker,[&]{result=QString("lease=%1 job=%2 focus=%3 firing=%4 paused=%5")
            .arg(worker->m_accessory.activeLease()).arg(worker->m_jobActive).arg(worker->m_focusActive)
            .arg(worker->m_core->firing).arg(worker->m_core->paused);},Qt::BlockingQueuedConnection);
        return result;
    }
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
public slots:
    void slowPageTransition(){QThread::msleep(300);}
private slots:
    void initTestCase() {
        qmlRegisterType<Workspace>("BlazerStudio",1,0,"Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio",1,0,"ReliefPreview");
    }
    void actualControlsMachineAndWorkerEndToEnd(){
        QTemporaryDir isolatedData;QVERIFY(isolatedData.isValid());
        const auto previousData=qgetenv("XDG_DATA_HOME");qputenv("XDG_DATA_HOME",isolatedData.path().toUtf8());
        const auto restoreData=qScopeGuard([&]{if(previousData.isNull())qunsetenv("XDG_DATA_HOME");else qputenv("XDG_DATA_HOME",previousData);});
        const auto identifiedPlant=isolatedData.filePath("identified-plant.py");
        QFile plant(identifiedPlant);QVERIFY(plant.open(QIODevice::WriteOnly));
        plant.write("import sys\nsys.path.insert(0,'" BLAZER_TEST_SOURCE_DIR "/tests/support')\nfrom m3_commissioning_peer import Plant\nclass IdentifiedPlant(Plant):\n def command(self,line):\n  if line=='$I': self.reply('[MSG:synthetic:M3:MAC=02:11:22:33:44:55]\\r\\n')\n  super().command(line)\np=IdentifiedPlant()\ntry:p.run()\nfinally:p.close()\n");plant.close();
        RuntimePeer peer;QVERIFY(peer.start(identifiedPlant).ok);
        Document document;document.setBackgroundPlanning(false);document.newProject();document.addShape("rect");
        document.setProperty("x",5.);document.setProperty("y",5.);document.setProperty("width",5.);document.setProperty("height",5.);
        QVERIFY(document.setStartMode(Document::AbsoluteCoordinates));
        Machine machine(&document);QVERIFY(machine.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);QTRY_VERIFY(machine.ready());QTRY_VERIFY(machine.jobPrepared());
        QQmlEngine engine;engine.rootContext()->setContextProperty("actualMachine",&machine);
        engine.rootContext()->setContextProperty("bridgeEndpoint",peer.port());
        QString qmlDir=qEnvironmentVariable("BLAZER_REAL_COMMISSIONING_QML_DIR");if(qmlDir.isEmpty())qmlDir=QStringLiteral(BLAZER_TEST_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        component.setData(QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {
 width:1200; height:1000; visible:true
 property alias setup: setup
 App.M3CommissioningDialog { id:setup; modelMachine:actualMachine }
 App.DeviceInitializationPanel { anchors.fill:parent; modelMachine:actualMachine; visible:actualMachine.commissioning.stage !== "complete" }
 App.ExtractionSupervisionPanel { id:supervision; anchors.fill:parent; visible:actualMachine.commissioning.stage === "complete"; modelMachine:actualMachine; commissioningComplete:actualMachine.commissioning.stage === "complete" }
 Button { id:start; objectName:"bridgeStart"; x:900; y:850; text:"Start"; property bool nativeActionEnabled:true; Component.onCompleted:actualMachine.registerNativeActionControl(start,"start"); onClicked:actualMachine.start() }
 Button { id:startAttended; objectName:"bridgeStartAttended"; x:750; y:850; text:"Start attended"; property bool nativeActionEnabled:true; property string nativeActionRole:"startAttended"; Component.onCompleted:{actualMachine.registerNativeActionControl(startAttended,"start");actualMachine.registerNativeActionControl(startAttended,"startAttended")} onClicked:actualMachine.startAttended() }
 Button { id:resume; objectName:"bridgeResume"; x:1000; y:850; text:"Resume"; property bool nativeActionEnabled:true; Component.onCompleted:actualMachine.registerNativeActionControl(resume,"resume"); onClicked:actualMachine.pause() }
 Button { id:referenceHome; objectName:"bridgeReferenceHome"; x:900; y:850; text:"Reference Home"; property bool nativeActionEnabled:true; property string nativeActionRole:"referenceHome"; Component.onCompleted:actualMachine.registerNativeActionControl(referenceHome,"referenceHome"); onClicked:actualMachine.referenceHome() }
 Button { id:connectHome; objectName:"bridgeConnectHome"; x:900; y:930; text:"Connect and Home"; property bool nativeActionEnabled:true; property var nativeActionArguments:({endpoint:bridgeEndpoint,binding:"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}); Component.onCompleted:actualMachine.registerNativeActionControl(connectHome,"connectHome") }
})").arg(QUrl::fromLocalFile(qmlDir).toString()).toUtf8(),QUrl());
        std::unique_ptr<QObject> root(component.create());QVERIFY2(root,qPrintable(component.errorString()));
        auto window=qobject_cast<QQuickWindow *>(root.get());QVERIFY(window);window->requestActivate();QVERIFY(QTest::qWaitForWindowActive(window,1000));
        auto item=[&](const QString&name){return find(window->contentItem(),name);};
        auto invoke=[&](const QString&name){QCoreApplication::processEvents();auto control=item(name);return control&&control->isEnabled()&&QMetaObject::invokeMethod(control,"clicked");};
        auto check=[&](const QString&name){QCoreApplication::processEvents();auto control=item(name);return control&&control->setProperty("checked",true);};
        auto native=[&](const QString&name,const QString&action,const QVariantMap&args=QVariantMap{}) {
            auto control=item(name);
            // Wait for the ordered data acknowledgement and QML enable binding
            // BEFORE creating one fresh input; never retry or refresh that input.
            if(!QTest::qWaitFor([&]{return control&&control->isEnabled()&&!machine.requestPending();},1000))return false;
            return ControllerRuntimeTestAccess::seed(machine,*control,action,args)&&QMetaObject::invokeMethod(control,"clicked");
        };
        auto stage=[&]{auto setup=root->property("setup").value<QObject*>();return setup?setup->property("stage").toString():QString();};
        auto setup=root->property("setup").value<QObject*>();QVERIFY(setup);QVERIFY(QMetaObject::invokeMethod(setup,"open"));QTRY_COMPARE(stage(),QString("mechanics"));
        for(const auto name:{"setupSwitchesWitness","setupDirectionWitness","setupFreeTravelWitness","setupLaserDisabledWitness"})QVERIFY(check(name));
        QVERIFY(invoke("attestSetupMechanics"));QTRY_VERIFY2(stage()=="homeReady",qPrintable(machine.commissioning().value("reason").toString()+" / "+ControllerRuntimeTestAccess::workerState(machine)+" / "+machine.log().join(" | ")));
        QCoreApplication::processEvents();
        QVERIFY2(NativeInputIntentTestAccess::hit(window,item("setupHomeButton")),
            "The actual modal Home button must be reachable by the native input hit test");
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
        QVERIFY(native("setupZeroButton","setupZero"));
        const auto zeroSettled=[&]{return stage()=="workCoordinates" && !machine.requestPending() && !machine.busy() && machine.ready() && machine.positionReferenced() && std::abs(machine.x())<.001 && std::abs(machine.y())<.001;};
        QTRY_VERIFY(zeroSettled());
        const auto resets=[&]{int n=0;for(const auto &entry:peer.snapshot().object.value("events").toArray()){auto e=entry.toObject();if(e.value("kind")=="realtime"&&e.value("value").toInt()==0x18)++n;}return n;};
        const auto beforeFinishResets=resets();
        {
            auto gate=std::make_shared<ControllerRuntimeTestAccess::Gate>();
            const auto release=qScopeGuard([&]{gate->release.release();});
            QVERIFY(ControllerRuntimeTestAccess::holdWorker(machine,gate));
            QVERIFY(check("setupCoordinatesWitness"));QVERIFY(invoke("confirmSetupCoordinates"));
            QCOMPARE(stage(),QString("workCoordinates"));
            QVERIFY(setup->property("visible").toBool());
            QVERIFY(!item("closeSetup")->isVisible());
            QVERIFY(!invoke("cancelSetup"));
            QCOMPARE(resets(),beforeFinishResets);
        }
        QTRY_COMPARE(stage(),QString("complete"));QTRY_VERIFY(!setup->property("visible").toBool());
        QVERIFY(machine.positionReferenced());QVERIFY(machine.motionSafetyError().isEmpty());
        QCOMPARE(resets(),beforeFinishResets);
        QVERIFY(QMetaObject::invokeMethod(setup,"open"));QTRY_VERIFY(setup->property("visible").toBool());
        QCOMPARE(stage(),QString("complete"));QVERIFY(invoke("closeSetup"));QTRY_VERIFY(!setup->property("visible").toBool());
        QVERIFY(machine.positionReferenced());QCOMPARE(resets(),beforeFinishResets);
        QTRY_VERIFY(machine.commissioning().value("calibrationStored").toBool());
        QTRY_VERIFY2(machine.extractionSupervision().value("referenceReady").toBool(),qPrintable(machine.extractionSupervision().value("referenceError").toString()));
        const auto powerBeforeReference=count(peer,"powered"),homesBeforeReference=count(peer,"homed");
        machine.referenceHome();QTest::qWait(60);QCOMPARE(count(peer,"homed"),homesBeforeReference);
        QVERIFY(native("bridgeReferenceHome","referenceHome"));
        QTRY_COMPARE(count(peer,"homed"),homesBeforeReference+1);
        QTRY_VERIFY(!machine.occupied());QCOMPARE(count(peer,"powered"),powerBeforeReference);

        QTRY_VERIFY2(machine.extractionSupervision().value("attendedStartReady").toBool(),qPrintable(machine.extractionSupervision().value("attendedStartError").toString()+" / "+machine.commissioning().value("stage").toString()+" / "+machine.commissioning().value("reason").toString()+" / "+machine.extractionSupervision().value("startSequenceReason").toString()+" / "+machine.log().join(" | ")));
        root.reset(); // Replace the bridge window; do not accumulate two applications' controls.
        // Exercise the real Main toolbar and its page transition, not only the
        // small bridge controls. The only connected endpoint is this test's PTY.
        {
            MaterialLibrary materials(isolatedData.filePath("main-materials.json"));
            Registration registration;
            ApplicationServices services(&document,&machine,isolatedData.filePath("main-settings.ini"));
            QVERIFY(services.finishOnboarding());
            QQmlEngine mainEngine;
            mainEngine.rootContext()->setContextProperty("project",&document);
            mainEngine.rootContext()->setContextProperty("machine",&machine);
            mainEngine.rootContext()->setContextProperty("materialLibrary",&materials);
            mainEngine.rootContext()->setContextProperty("cameraRegistration",&registration);
            mainEngine.rootContext()->setContextProperty("appServices",&services);
            QQmlComponent mainComponent(&mainEngine,QUrl::fromLocalFile(QStringLiteral(BLAZER_TEST_SOURCE_DIR "/qml/Main.qml")));
            std::unique_ptr<QObject> mainRoot(mainComponent.create());QVERIFY2(mainRoot,qPrintable(mainComponent.errorString()));
            auto mainWindow=qobject_cast<QQuickWindow*>(mainRoot.get());QVERIFY(mainWindow);
            mainWindow->requestActivate();QVERIFY(QTest::qWaitForWindowActive(mainWindow));
            auto mainItem=[&](const QString &name){return find(mainWindow->contentItem(),name);};
            QTRY_VERIFY(mainRoot->findChild<QObject*>("startupSafetyDialog")->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(mainItem("acknowledgeStartupSafety"),"clicked"));
            QTRY_VERIFY(!mainRoot->findChild<QObject*>("startupSafetyDialog")->property("visible").toBool());
            auto mainStart=mainItem("startJobButton");QVERIFY(mainStart);
            QTRY_VERIFY2(mainStart->isEnabled(),qPrintable(mainRoot->property("startBlockReason").toString()));
            QVERIFY2(NativeInputIntentTestAccess::hit(mainWindow,mainStart),"Main Start must be reachable by native hit testing");
            // A slower first layout must not age the native input before dispatch.
            QVERIFY(connect(mainRoot.get(),SIGNAL(pageChanged()),this,SLOT(slowPageTransition()),Qt::DirectConnection));
            const auto before=count(peer,"powered");
            QVERIFY(ControllerRuntimeTestAccess::seed(machine,*mainStart,"startAttended"));
            QVERIFY(QMetaObject::invokeMethod(mainStart,"clicked"));
            QTRY_VERIFY2_WITH_TIMEOUT(count(peer,"powered")>before,qPrintable(document.message()+" / "+machine.log().join(" | ")),3000);
            QTRY_VERIFY(!machine.occupied());
            QCOMPARE(mainRoot->property("page").toInt(),2);
            QVERIFY(mainItem("liveJobPanel")->isVisible());
            // Programmatic activation has no new input. Explain rejection in
            // the main view, retain the finished preview and never run twice.
            const auto after=count(peer,"powered");
            QTRY_VERIFY(mainStart->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(mainStart,"clicked"));
            QTRY_VERIFY(mainItem("startActionStatus")->isVisible());
            QVERIFY(!mainRoot->property("startActionError").toString().isEmpty());
            QTest::qWait(80);QCOMPARE(count(peer,"powered"),after);
            mainWindow->hide();
        }
        root.reset(component.create());QVERIFY(root);window=qobject_cast<QQuickWindow*>(root.get());QVERIFY(window);
        setup=root->property("setup").value<QObject*>();QVERIFY(setup);
        window->requestActivate();QVERIFY(QTest::qWaitForWindowActive(window));
        const auto poweredBeforeSingle=count(peer,"powered");
        machine.startAttended();QTest::qWait(60);QCOMPARE(count(peer,"powered"),poweredBeforeSingle);
        // One genuine-shaped native intent, no clearance, mode selection or
        // separate presence data request. The independent PTY sees the job.
        QVERIFY(native("bridgeStartAttended","startAttended"));QTRY_VERIFY(count(peer,"powered")>poweredBeforeSingle);QTRY_VERIFY(!machine.occupied());
        QCOMPARE(count(peer,"collision"),0);
        const auto firstJobPower=count(peer,"powered");
        machine.startAttended();QTest::qWait(60);QCOMPARE(count(peer,"powered"),firstJobPower);
        // A subsequent job needs only its own new native Start, no reset or
        // clearance after normal device-cycle completion.
        QTRY_VERIFY(machine.extractionSupervision().value("attendedStartReady").toBool());
        QVERIFY(native("bridgeStartAttended","startAttended"));
        QTRY_VERIFY(count(peer,"powered")>firstJobPower);QTRY_VERIFY(!machine.occupied());
        const auto poweredAfterSingle=count(peer,"powered");
        QVERIFY(check("extractionClearanceWitness"));QVERIFY(invoke("confirmExtractionClearance"));QTRY_VERIFY(machine.extractionSupervision().value("reason").toString().startsWith("Sicheren Zustand"));
        QVERIFY(invoke("selectAttendedExtraction"));QTRY_COMPARE(machine.extractionSupervision().value("mode").toString(),QString("operatorAttendedExternal"));
        auto attest=[&](){return check("externalExtractionOnWitness")&&check("operatorPresentWitness")&&check("attendedWorkAreaWitness")&&invoke("attestExternalExtraction");};
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("start"));
        QVERIFY(native("bridgeStart","start"));QTRY_VERIFY(count(peer,"powered")>poweredAfterSingle);QTRY_VERIFY(!machine.occupied());
        QCOMPARE(count(peer,"collision"),0);QVERIFY(machine.extractionSupervision().value("pendingAction").toString().isEmpty());
        const auto before=count(peer,"powered");machine.start();QTest::qWait(80);QCOMPARE(count(peer,"powered"),before); // No new native activation/witness.
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("start"));
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",false}}}}).ok);
        QVERIFY(native("bridgeStart","start"));QTRY_VERIFY(machine.busy());machine.pause();QTRY_VERIFY(machine.holdConfirmed());
        QTRY_COMPARE(item("extractionActionChoice")->property("currentValue").toString(),QString("resume"));
        QVERIFY(attest());QTRY_COMPARE(machine.extractionSupervision().value("pendingAction").toString(),QString("resume"));
        QVERIFY(native("bridgeResume","resume"));QTRY_VERIFY(!machine.paused());
        // The GUI state can update before the independent peer consumes the serial byte.
        const auto resumeReceived=[&]{for(const auto &entry:peer.snapshot().object.value("events").toArray()){const auto e=entry.toObject();if(e.value("kind")=="realtime"&&e.value("value").toInt()==int('~'))return true;}return false;};
        QTRY_VERIFY(resumeReceived());
        QVERIFY(invoke("revokeExternalAttendance"));QTRY_COMPARE(stage(),QString("fault"));QVERIFY(!machine.positionReferenced());QVERIFY(!machine.motionSafetyError().isEmpty());
        QTRY_VERIFY(machine.extractionSupervision().value("clearanceRequired").toBool());
        const auto poweredAfterFault=count(peer,"powered");
        machine.startAttended();QTest::qWait(60);
        QCOMPARE(count(peer,"powered"),poweredAfterFault);
        QVERIFY(machine.extractionSupervision().value("clearanceRequired").toBool());
        machine.useSimulator();QTRY_VERIFY(machine.simulation()&&machine.connectionPhase()=="closed");
        machine.setPreviewDeviceBinding(QByteArray(64,'a'));
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",true}}}}).ok);
        const auto homesBeforeAuto=count(peer,"homed"),movesBeforeAuto=count(peer,"physicalMove"),powerBeforeAuto=count(peer,"powered");
        // Neither a preference nor a programmatic invocation authorizes motion.
        QVERIFY(!machine.requestConnectHardware(peer.port(),QByteArray(64,'a'),true).enqueued);
        QCOMPARE(count(peer,"homed"),homesBeforeAuto);
        QVERIFY2(NativeInputIntentTestAccess::hit(window,item("bridgeConnectHome")),"Connect-and-Home control must be reachable");
        QVERIFY(native("bridgeConnectHome","connectHome",{{"endpoint",peer.port()},{"binding",QString(64,'a')}}));
        QVERIFY(machine.requestConnectHardware(peer.port(),QByteArray(64,'a'),true).enqueued);
        QTRY_VERIFY2(stage()=="savedApproachReady",qPrintable(machine.commissioning().value("reason").toString()+" / "+machine.log().join(" | ")));
        QCOMPARE(count(peer,"homed"),homesBeforeAuto+1);
        QCOMPARE(count(peer,"physicalMove"),movesBeforeAuto);QCOMPARE(count(peer,"powered"),powerBeforeAuto);
        machine.useSimulator();QTRY_VERIFY(machine.simulation()&&machine.connectionPhase()=="closed");
        // The normal public reconnect loads the saved DATA. The real QML compact
        // controls must still produce a new typed native Home and approach intent.
        const auto homesBeforeReconnect=count(peer,"homed"),poweredBeforeReconnect=count(peer,"powered");
        QVERIFY(peer.control({{"action","set"},{"values",QJsonObject{{"ackEnabled",true}}}}).ok);
        QVERIFY(machine.requestConnectHardware(peer.port(),QByteArray(64,'a')).enqueued);
        QTRY_VERIFY2(stage()=="savedHomeReady",qPrintable(machine.commissioning().value("reason").toString()+" / "+machine.log().join(" | ")));
        QCOMPARE(count(peer,"homed"),homesBeforeReconnect);QVERIFY(!machine.positionReferenced());
        QVERIFY2(NativeInputIntentTestAccess::hit(window,item("initializeSavedHome")),"Compact reference button must be reachable");
        QVERIFY(native("initializeSavedHome","setupHome"));QTRY_COMPARE(stage(),QString("savedApproachReady"));
        QVERIFY2(NativeInputIntentTestAccess::hit(window,item("initializeSavedApproach")),"Compact approach button must be reachable");
        QVERIFY(native("initializeSavedApproach","setupCorner",{{"index",0}}));
        QTRY_VERIFY(stage()=="complete"||stage()=="restoredOriginReview");
        if(stage()=="restoredOriginReview") {
            QVERIFY(QMetaObject::invokeMethod(setup,"open"));
            QVERIFY(check("setupCoordinatesWitness"));QVERIFY(invoke("confirmSetupCoordinates"));
            QTRY_COMPARE(stage(),QString("complete"));QTRY_VERIFY(!setup->property("visible").toBool());
        }
        QVERIFY(machine.positionReferenced());QVERIFY(machine.motionSafetyError().isEmpty());
        QCOMPARE(count(peer,"homed"),homesBeforeReconnect+1);QCOMPARE(count(peer,"powered"),poweredBeforeReconnect);
        QCOMPARE(count(peer,"collision"),0);
        machine.useSimulator();QTRY_VERIFY(machine.simulation()&&machine.connectionPhase()=="closed");
    }
};
QTEST_MAIN(CommissioningMachineBridgeTest)
#include "commissioningmachinebridge_test.moc"
