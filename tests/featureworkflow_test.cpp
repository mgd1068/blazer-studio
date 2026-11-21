#include "applicationservices.h"
#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "materiallibrary.h"
#include "materialtestledger.h"
#include "registration.h"
#include "reliefpreview.h"
#include "workspace.h"
#include "vectorinspectionservice.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *item = find(child, name)) return item;
    return nullptr;
}
bool click(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 1000)) return false;
    QTest::qWait(40);
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid()) continue;
        const auto p = item->mapToItem(parent, QPointF());
        const auto delta = p.y() < 5 ? p.y() - 5 : p.y() + item->height() > parent->height() - 5
            ? p.y() + item->height() - parent->height() + 5 : 0;
        const auto limit = std::max(0., parent->property("contentHeight").toDouble() - parent->height());
        parent->setProperty("contentY", std::clamp(parent->property("contentY").toDouble() + delta, 0., limit));
    }
    QTest::qWait(50);
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto p = item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
        if (!QRect(0, 0, window->width(), window->height()).contains(p)) return false;
        QTest::mouseMove(window, p); QTest::qWait(20);
        if (item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint() == p) {
            QTest::mouseClick(window, Qt::LeftButton, {}, p); return true;
        }
    }
    return false;
}
}

class FeatureWorkflowTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio",1,0,"Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio",1,0,"ReliefPreview");
    }
    void realApplicationFeatures_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("normal-light") << false;
        QTest::newRow("compact-dark") << true;
    }
    void realApplicationFeatures() {
        QFETCH(bool, compactDark);
        QTemporaryDir storage; QVERIFY(storage.isValid());
        Document project; project.setBackgroundPlanning(false); project.newProject();
        QVERIFY(project.applyBasicMaterial(MaterialLibrary::example())); project.addShape("rect");
        Machine machine(&project, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager devices(&project, &machine, storage.filePath("devices.json"));
        MaterialTestLedger ledger(storage.filePath("material-tests.json"));
        MaterialLibrary library(storage.filePath("materials.json"));
        VectorInspectionService vectorInspector(&project);
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding()); QVERIFY(services.setDarkTheme(compactDark));
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project); context->setContextProperty("machine", &machine);
        context->setContextProperty("deviceManager", &devices); context->setContextProperty("materialTestLedger", &ledger);
        context->setContextProperty("materialLibrary", &library); context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        context->setContextProperty("vectorInspectionService", &vectorInspector);
        for (const auto &pair : {qMakePair("deviceProfile", "blaze-x-m3.json"),qMakePair("manufacturerReference", "manufacturer-materials.json")}) {
            QFile file(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/") + pair.second);
            QVERIFY(file.open(QIODevice::ReadOnly));
            context->setContextProperty(pair.first, QJsonDocument::fromJson(file.readAll()).toVariant());
        }
        engine.load(QUrl::fromLocalFile(QStringLiteral(BLAZER_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        if (compactDark) window->resize(1180,760);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](const QString &name) { return find(window->contentItem(),name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(QString::fromLatin1(name)); };
        auto press = [&](const QString &name) { return click(window,item(name)); };
        auto text = [&](const QString &name, const QString &value) {
            auto *input = item(name); return input && input->setProperty("text",value);
        };
        auto raw = [&](const QString &name, const QString &value) {
            auto *field = item(name); return field && field->setProperty("inputText",value);
        };
        auto picture = [&](const QString &stage) {
            if (!qEnvironmentVariableIsSet("BLAZER_FEATURE_SCREENSHOT_DIR")) return true;
            const auto folder = qEnvironmentVariable("BLAZER_FEATURE_SCREENSHOT_DIR");
            if (!QDir().mkpath(folder)) return false;
            QTest::qWait(80);
            return window->grabWindow().save(folder+"/"+QTest::currentDataTag()+"-"+stage+".png");
        };
        auto fileResult = [&](const char *name, const QString &path) {
            auto *picker = object(name);
            return picker && picker->setProperty("selectedFile",QUrl::fromLocalFile(path)) &&
                QMetaObject::invokeMethod(picker,"accepted") && QMetaObject::invokeMethod(picker,"reject");
        };
        auto settingsMenu = [&] {
            if (!press("projectDetailsButton")) return false;
            if (!QTest::qWaitFor([&]{ return item("settingsBundleMenuItem") && item("settingsBundleMenuItem")->isVisible(); },1000)) return false;
            return press("settingsBundleMenuItem");
        };
        QTRY_VERIFY(object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(press("acknowledgeStartupSafety"));
        QTRY_VERIFY(!object("startupSafetyDialog")->property("visible").toBool());
        QCOMPARE(window->property("night").toBool(),compactDark);
        QVERIFY(window->property("basic").toBool());

        QVERIFY(press("manageDevicesButton"));
        QTRY_VERIFY(object("deviceManagerDialog")->property("visible").toBool());
        QVERIFY(text("deviceInstanceName","<b>Werkbank A</b>")); QVERIFY(text("deviceInstanceEndpoint","/dev/never-open-A"));
        QVERIFY(press("addCurrentDevice")); QCOMPARE(devices.instances().size(),1); QVERIFY(devices.activeId().isEmpty());
        QVERIFY(press("activateDeviceInstance")); QVERIFY(!devices.activeId().isEmpty());
        const auto firstBinding = devices.bindingKey();
        window->setProperty("proofConfirmed",true);
        QVERIFY(press("manageDevicesButton")); QVERIFY(press("duplicateDeviceInstance"));
        QVERIFY(text("deviceInstanceName","Werkbank B")); QVERIFY(text("deviceInstanceEndpoint","/dev/never-open-B"));
        QTRY_VERIFY(!item("activateDeviceInstance")->isEnabled());
        QVERIFY(press("saveDeviceInstance")); QTRY_VERIFY(item("activateDeviceInstance")->isEnabled());
        QVERIFY(press("activateDeviceInstance"));
        QVERIFY(devices.bindingKey()!=firstBinding);
        QTRY_VERIFY(!window->property("proofConfirmed").toBool());
        QVERIFY(machine.simulation()); QVERIFY(machine.ports().isEmpty());

        QVERIFY(press("jobPlacementButton"));
        QTRY_VERIFY(object("jobPlacementDialog")->property("visible").toBool());
        auto *mode = item("startModeChoice"); QVERIFY(mode);
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,1)));
        QCOMPARE(project.startMode(),Document::CurrentPosition);
        for (int anchor=0;anchor<9;++anchor) {
            QVERIFY(press(QString("jobOrigin%1").arg(anchor)));
            QCOMPARE(int(project.jobOrigin()),anchor);
            QVERIFY2(project.job().valid(),qPrintable(project.job().errors.join("; ")));
        }
        QVERIFY(picture("current-position"));
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,2)));
        QVERIFY(!project.job().valid());
        QVERIFY(press("captureJobUserOrigin"));
        QVERIFY(project.placementSummary()["originConfirmed"].toBool());
        QVERIFY(project.job().valid()); QVERIFY(picture("user-origin"));
        QVERIFY(QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,0)));
        QVERIFY(QMetaObject::invokeMethod(object("jobPlacementDialog"),"close"));

        const auto beforeBarcode = project.count();
        QVERIFY(press("barcodeToolButton"));
        QVERIFY(text("barcodeText","HELLO42")); QVERIFY(raw("barcodeModuleWidth","0.3 mm"));
        QVERIFY(raw("barcodeHeight","12 mm")); QVERIFY(raw("barcodeLeft","10 mm")); QVERIFY(raw("barcodeBottom","10 mm"));
        QVERIFY(press("createBarcodeButton")); QTRY_COMPARE(project.count(),beforeBarcode+1);
        QVERIFY(project.items().last().style.value("barcode").toBool()); QVERIFY(project.job().valid());
        QVERIFY(picture("barcode"));

        const auto nativePath = storage.filePath("review.lbrn2");
        QFile nativeFile(nativePath); QVERIFY(nativeFile.open(QIODevice::WriteOnly));
        const QByteArray foreign = "<LightBurnProject><CutSetting type='Cut'><maxPower Value='100'/><speed Value='1'/></CutSetting><Shape Type='Rect' W='10' H='5'><XForm>1 0 0 1 20 20</XForm></Shape></LightBurnProject>";
        QCOMPARE(nativeFile.write(foreign),foreign.size()); nativeFile.close();
        const auto beforeNativeCount=project.count(); const auto beforeNativeCode=project.job().gcode();
        QVERIFY(press("importMotifButton")); QVERIFY(fileResult("importMotifDialog",nativePath));
        QTRY_VERIFY(object("nativeImportReviewDialog")->property("visible").toBool());
        QVERIFY(item("confirmNativeGeometryImport")->isEnabled());
        QVERIFY(picture("native-review")); QVERIFY(press("confirmNativeGeometryImport"));
        QTRY_COMPARE(project.count(),beforeNativeCount+1);
        QCOMPARE(project.items().last().toolLayer,1); QCOMPARE(project.job().gcode(),beforeNativeCode);
        project.setBasicOperation("outline"); QCOMPARE(project.items().last().toolLayer,0);
        QVERIFY(press("vectorInspectionButton"));
        QTRY_VERIFY(object("vectorInspectionDialog")->property("visible").toBool());
        QVERIFY(press("inspectAllVectors"));
        QTRY_VERIFY_WITH_TIMEOUT(!vectorInspector.busy(),5000);
        QVERIFY(vectorInspector.report()["complete"].toBool());
        QVERIFY(picture("vector-inspection"));
        QVERIFY(QMetaObject::invokeMethod(object("vectorInspectionDialog"),"close"));

        QVERIFY(press("materialTestHistoryButton"));
        QTRY_VERIFY(object("materialTestRecordDialog")->property("visible").toBool());
        QVERIFY(text("materialObservationName","Fixture plywood")); QVERIFY(text("materialObservationBatch","fixture batch"));
        QVERIFY(raw("materialObservationThickness","3.2 mm"));
        QVERIFY(text("materialObservationNotes","Synthetic UI observation only, no physical material test or device grant."));
        QVERIFY(press("saveMaterialObservation")); QTRY_COMPARE(ledger.records().size(),1);
        QCOMPARE(ledger.records()[0].toMap()["bindingKey"].toString(),devices.bindingKey());
        QVERIFY(!window->property("proofConfirmed").toBool());
        QTRY_VERIFY(item("materialMatchingRecordCount")->property("text").toString().endsWith(": 1"));
        QVERIFY(picture("material-history"));
        QVERIFY(text("materialObservationNotes","Another observation")); QTRY_VERIFY(item("saveMaterialObservation")->isEnabled());
        project.setLayerSetting(0,"power",23.);
        QTRY_VERIFY(!item("saveMaterialObservation")->isEnabled());
        QVERIFY(!object("materialTestRecordDialog")->property("sameContext").toBool());
        QVERIFY(QMetaObject::invokeMethod(object("materialTestRecordDialog"),"close"));

        QVERIFY(settingsMenu()); QTRY_VERIFY(object("settingsBundleDialog")->property("visible").toBool());
        QVERIFY(press("exportSettingsBundle"));
        const auto bundle = storage.filePath("portable-ui.json");
        QVERIFY(fileResult("settingsExportFileDialog",bundle)); QVERIFY(QFile::exists(bundle));
        QVERIFY(services.setLengthUnit("in")); QVERIFY(services.setDarkTheme(!compactDark)); QVERIFY(services.setBeginnerMode(false));
        QTRY_VERIFY(!window->property("basic").toBool());
        QCOMPARE(window->property("night").toBool(),!compactDark);
        QVERIFY(press("chooseSettingsBundle")); QVERIFY(fileResult("settingsImportFileDialog",bundle));
        QTRY_VERIFY(item("applySettingsBundle")->isEnabled());
        QVERIFY(picture("settings-preview")); QVERIFY(press("applySettingsBundle"));
        QTRY_COMPARE(services.lengthUnit(),QString("mm"));
        QTRY_VERIFY(window->property("basic").toBool());
        QCOMPARE(window->property("night").toBool(),compactDark);
        QVERIFY(QMetaObject::invokeMethod(object("settingsBundleDialog"),"close"));

        window->setProperty("page",2);
        QTRY_VERIFY(item("frameJobButton")->isEnabled());
        QVERIFY(press("frameJobButton")); QTRY_VERIFY(machine.busy()); QVERIFY(!machine.firing());
        QVERIFY(settingsMenu()); QTRY_VERIFY(object("settingsBundleDialog")->property("visible").toBool());
        QVERIFY(machine.busy());
        QTest::keyClick(window,Qt::Key_Period,Qt::ControlModifier);
        QTRY_VERIFY(machine.ready()); QVERIFY(!machine.busy());
        QVERIFY(!machine.firing()); QVERIFY(machine.simulation());
        QVERIFY(!window->property("proofConfirmed").toBool());
        QVERIFY(picture("stopped-through-modal"));
    }
};
QTEST_MAIN(FeatureWorkflowTest)
#include "featureworkflow_test.moc"
