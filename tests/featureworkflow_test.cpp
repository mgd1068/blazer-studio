#include "applicationservices.h"
#include "artlibrary.h"
#include "artlibrarycontroller.h"
#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "materiallibrary.h"
#include "materialtestledger.h"
#include "registration.h"
#include "reliefpreview.h"
#include "support/runtimepeer.h"
#include "vectorinspectionservice.h"
#include "workspace.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using RuntimeTestSupport::RuntimePeer;
namespace {
QString peerScript() {
    return QFileInfo(QString::fromUtf8(__FILE__))
        .absoluteDir()
        .filePath("support/runtime_pty_peer.py");
}
bool configurePeer(RuntimePeer &peer, const QJsonObject &values) {
    const auto reply = peer.control({{"action", "set"}, {"values", values}});
    return reply.ok && reply.object["accepted"].toBool();
}
} // namespace

namespace {
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *item = find(child, name)) return item;
    return nullptr;
}
bool click(QQuickWindow *window, QQuickItem *item, bool requireEnabled = true) {
    if (!item || !item->isVisible() || (requireEnabled && !item->isEnabled()))
        return false;
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

// Readiness-only UI fixture. C++ Machine stays hardware-disabled/simulated;
// these Qt status properties exercise Main's live readiness explanation without
// manufacturing any runtime reference, native proof or execution permission.
class AttendedReadinessMachine : public Machine {
    Q_OBJECT
    Q_PROPERTY(bool simulation READ uiSimulation NOTIFY uiChanged)
    Q_PROPERTY(bool connected READ uiConnected NOTIFY uiChanged)
    Q_PROPERTY(bool connectionVerified READ uiConnected NOTIFY uiChanged)
    Q_PROPERTY(bool statusFresh READ uiConnected NOTIFY uiChanged)
    Q_PROPERTY(QString connectionPhase READ uiPhase NOTIFY uiChanged)
    Q_PROPERTY(bool jobPreparing READ uiFalse NOTIFY uiChanged)
    Q_PROPERTY(bool jobPrepared READ uiConnected NOTIFY uiChanged)
    Q_PROPERTY(bool positionReferenced READ uiConnected NOTIFY uiChanged)
    Q_PROPERTY(QString motionSafetyError READ uiEmpty NOTIFY uiChanged)
    Q_PROPERTY(QStringList processingSafetyErrors READ oldWitnessErrors NOTIFY uiChanged)
    Q_PROPERTY(QVariantMap commissioning READ uiCommissioning NOTIFY uiChanged)
    Q_PROPERTY(QVariantMap extractionSupervision READ uiExtraction NOTIFY uiChanged)
    Q_PROPERTY(QVariantMap operatorLocalFrame READ uiLocalFrame NOTIFY uiChanged)
    Q_PROPERTY(QVariantMap liveJob READ uiLiveJob NOTIFY uiChanged)
public:
    explicit AttendedReadinessMachine(Document *document):Machine(document,nullptr,HardwareAccess::Disabled){}
    QString attendedError;
    QVariantMap liveState;
    QVariantMap uiLiveJob()const{return liveState;}
    void setLiveState(QVariantMap state){liveState=std::move(state);emit uiChanged();}
    bool uiSimulation()const{return false;}bool uiConnected()const{return true;}bool uiFalse()const{return false;}
    QString uiPhase()const{return "idle";}QString uiEmpty()const{return {};}
    QStringList oldWitnessErrors()const{return {"OLD_SEPARATE_WITNESS_MISSING"};}
    QVariantMap uiCommissioning()const{return {{"stage","complete"},{"workMinX",0.},{"workMinY",0.},{"workMaxX",200.},{"workMaxY",150.},{"activeInputs",0},{"controllerState","Idle"}};}
    QVariantMap uiLocalFrame()const{return {{"phase","inactive"}};}
    QVariantMap uiExtraction()const{return {{"mode","operatorSupervisedDeviceCycle"},{"pendingAction",""},{"singleClickStart",true},{"attendedStartReady",attendedError.isEmpty()},{"attendedStartError",attendedError},{"referenceReady",attendedError.isEmpty()},{"referenceError",attendedError}};}
    void setAttendedError(QString error){attendedError=std::move(error);emit uiChanged();}
signals:
    void uiChanged();
};

class FeatureWorkflowTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio",1,0,"Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio",1,0,"ReliefPreview");
    }
    void asynchronousRuntimeConnectionThroughRealMain_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("runtime-normal-light") << false;
        QTest::newRow("runtime-compact-dark") << true;
    }
    void asynchronousRuntimeConnectionThroughRealMain() {
        QFETCH(bool, compactDark);
        RuntimePeer peer;
        QVERIFY(peer.start(peerScript()).ok);
        QVERIFY(configurePeer(peer, {{"resetEnabled", false}}));
        QTemporaryDir storage;
        Document project;
        project.newProject();
        project.addShape("rect");
        Machine machine(
            &project); // Only peer.port(), created above by an independent synthetic process.
        DeviceManager devices(&project, &machine, storage.filePath("devices.json"));
        const auto a = devices.addCurrent("Synthetic runtime", peer.port());
        QVERIFY(devices.activate(a));
        MaterialLibrary materials(storage.filePath("materials.json"));
        MaterialTestLedger ledger(storage.filePath("ledger.json"));
        ArtLibrary arts(storage.filePath("arts.json"));
        ArtLibraryController artController(&project, &machine, &arts);
        VectorInspectionService vectors(&project);
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding());
        QVERIFY(services.setDarkTheme(compactDark));
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("deviceManager", &devices);
        context->setContextProperty("materialLibrary", &materials);
        context->setContextProperty("materialTestLedger", &ledger);
        context->setContextProperty("artLibrary", &arts);
        context->setContextProperty("artLibraryController", &artController);
        context->setContextProperty("vectorInspectionService", &vectors);
        context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(compactDark ? 1180 : 1480, compactDark ? 760 : 920);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](const QString &name) { return find(window->contentItem(), name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(name); };
        QTRY_VERIFY(object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("acknowledgeStartupSafety")));
        QTRY_VERIFY(!object("startupSafetyDialog")->property("visible").toBool());
        window->setProperty("page", 2);
        auto *connect = item("connectGrblButton");
        QVERIFY(connect);
        QTRY_VERIFY(connect->isEnabled());
        QVERIFY(click(window, connect));
        QTRY_VERIFY(object("connectionSafetyDialog")->property("visible").toBool());
        QVERIFY(!item("confirmInitialConnection")->isEnabled());
        QVERIFY(click(window,item("safeInitialConnection")));
        QTRY_VERIFY(item("confirmInitialConnection")->isEnabled());
        QVERIFY(click(window,item("confirmInitialConnection")));
        QTRY_VERIFY(!object("connectionSafetyDialog")->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!machine.simulation() && machine.connected(), 3000);
        QVERIFY(!machine.connectionVerified());
        QVERIFY(!window->property("grblVerified").toBool());
        QVERIFY(!connect->isEnabled());
        QVERIFY(machine.occupied());
        QVERIFY(!project.hasPreviewPosition());
        QTRY_VERIFY(
            item("grblConnectionStatus")
                ->property("text")
                .toString()
                .contains("prüf", Qt::CaseInsensitive) ||
            item("grblConnectionStatus")->property("text").toString().contains("bestätigt"));
        QVERIFY(!devices.changeBlockReason().isEmpty());
        QVERIFY(!item("focusHold")->isEnabled());
        QVERIFY(!item("homeControllerButton")->isEnabled());
        QVERIFY(peer.control({{"action", "reply"}, {"data", "Grbl 1.3a ['$' for help]\r\n"}})
                    .object["accepted"]
                    .toBool());
        QTRY_VERIFY_WITH_TIMEOUT(machine.connectionVerified(), 5000);
        QTRY_VERIFY(window->property("grblVerified").toBool());
        QVERIFY(machine.statusFresh());
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!window->property("positioningReady").toBool());
        QVERIFY(item("controllerPositionReadout")
                    ->property("text")
                    .toString()
                    .contains("nicht referenziert"));
        // Signal/binding changes without a native press are never a new emission intent.
        auto *focus = item("focusHold");
        QVERIFY(focus);
        QVERIFY(QMetaObject::invokeMethod(focus, "pressedChanged"));
        QVERIFY(QMetaObject::invokeMethod(focus, "pressed"));
        QVERIFY(!machine.firing());
        // Cancelling the unsaved-changes dialog must never leave a delayed close intent.
        window->close();
        QTRY_VERIFY(object("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("cancelCloseButton")));
        QTRY_VERIFY(!object("discardDialog")->property("visible").toBool());
        QVERIFY(window->isVisible());
        QVERIFY(!window->property("controllerClosePending").toBool());
        QVERIFY(machine.connected());
        QVERIFY(configurePeer(peer, {{"statusEnabled", false}}));
        QTRY_VERIFY_WITH_TIMEOUT(!machine.statusFresh(), 3000);
        QTRY_VERIFY(!window->property("grblVerified").toBool());
        QVERIFY(!window->property("processingReady").toBool());
        QVERIFY(!item("applyLiveOverrides")->isEnabled());
        QVERIFY(item("grblConnectionStatus")->property("text").toString().contains("veraltet"));
        QVERIFY(
            item("controllerPositionReadout")->property("text").toString().contains("unbekannt"));
        QVERIFY(!item("activePageFooter")->property("text").toString().contains("Bereit"));
        QVERIFY(item("controllerModeStatus")->property("text").toString().contains("veraltet"));
        QTRY_VERIFY(click(
            window, item("controllerPositionReadout"))); // Settle wrapped layout and reveal status/position controls.
        const auto folder = qEnvironmentVariable("BLAZER_RUNTIME_UI_EVIDENCE");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QTest::qWait(60);
            QVERIFY(window->grabWindow().save(folder + "/runtime-stale-" + QTest::currentDataTag() +
                                              ".png"));
        }
        QTRY_VERIFY(!machine.occupied());
        const auto clean = QUrl::fromLocalFile(storage.filePath("close-intent.blazer"));
        QVERIFY(project.save(clean));
        QVERIFY(!project.dirty());
        window->close();
        QVERIFY(window->isVisible());
        QVERIFY(window->property("controllerClosePending").toBool());
        QVERIFY(item("controllerCloseStatus")->isVisible());
        const auto closeIntent = window->property("controllerCloseIntent");
        window->close(); // Idempotent X while awaiting the matching port-close epoch.
        QCOMPARE(window->property("controllerCloseIntent"), closeIntent);
        if (compactDark) {
            // Retiring the old window visibility lifetime also retires its close intent.
            window->hide();
            QVERIFY(!window->property("controllerClosePending").toBool());
            window->show();
            QTRY_VERIFY_WITH_TIMEOUT(machine.simulation() && !machine.occupied(), 3000);
            QTest::qWait(100);
            QVERIFY(window->isVisible());
            QVERIFY(!object("discardDialog")->property("visible").toBool());
        }
        // A permitted light edit after the first X must not inherit the old clean-close consent.
        QVERIFY(project.setMetadata("Edited while port closure was pending", "Retain this edit"));
        if (compactDark) window->close(); // New explicit action, never the retired intent.
        QTRY_VERIFY_WITH_TIMEOUT(machine.simulation() && !machine.occupied(), 3000);
        QTRY_VERIFY(object("discardDialog")->property("visible").toBool());
        QVERIFY(window->isVisible());
        QVERIFY(!window->property("controllerClosePending").toBool());
        QVERIFY(click(window, item("cancelCloseButton")));
        QTRY_VERIFY(!object("discardDialog")->property("visible").toBool());
        QTest::qWait(100);
        QVERIFY(window->isVisible());
        QVERIFY(project.dirty());
        window->close();
        QTRY_VERIFY(object("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("discardChangesButton")));
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 3000);
        QVERIFY(machine.simulation());
        QCOMPARE(machine.connectionPhase(), QString("closed"));
        QVERIFY(!machine.occupied());
        QVERIFY(!machine.requestPending());
        const auto snapshot = peer.snapshot();
        QVERIFY2(snapshot.ok, qPrintable(snapshot.error));
        const auto wire = snapshot.object;
        QVERIFY(wire.contains("events"));
        for (const auto &event : wire["events"].toArray()) {
            const auto row = event.toObject();
            const auto command = row["value"].toString();
            if (row["kind"] == "command") {
                QVERIFY(!command.startsWith("G1 "));
                QVERIFY(!command.startsWith("M3"));
                QVERIFY(!command.startsWith("M4"));
                QVERIFY(command != "$H");
            }
        }
    }
    void synchronousWorkGatesThroughRealMain_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("guard-normal-light") << false;
        QTest::newRow("guard-compact-dark") << true;
    }
    void synchronousWorkGatesThroughRealMain() {
        QFETCH(bool, compactDark);
        QTemporaryDir storage;
        QString guardReason;
        Document project;
        project.newProject();
        project.addShape("rect");
        project.setLayer(1);
        project.setProperty("name", "Original next design");
        project.setProperty("width", 40.);
        project.setProperty("height", 20.);
        project.setLayerSetting(1, "speed", 60.);
        Machine machine(&project, nullptr, Machine::HardwareAccess::Disabled);
        project.addSynchronousWorkGuard([&] { return guardReason; });
        auto block = [&](bool active) {
            guardReason =
                active
                    ? QString("Test-Sperre: Laufende Gerätekommunikation hat Vorrang. Dateien, "
                              "Import und Artwork-Zwischenablage nach Ende erneut ausdrücklich "
                              "ausführen; leichte Änderungen am nächsten Entwurf bleiben möglich.")
                    : QString();
            project.notifySynchronousWorkGuardChanged();
        };
        DeviceManager devices(&project, &machine, storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));
        MaterialTestLedger ledger(storage.filePath("ledger.json"));
        ArtLibrary arts(storage.filePath("arts.json"));
        ArtLibraryController artController(&project, &machine, &arts);
        VectorInspectionService vectors(&project);
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding());
        QVERIFY(services.setDarkTheme(compactDark));
        const auto initialFile = QUrl::fromLocalFile(storage.filePath("initial.blazer"));
        QVERIFY(project.save(initialFile));
        const auto initialBytes = project.projectBytes();
        QVERIFY(!initialBytes.isEmpty());
        Document other;
        other.newProject();
        other.addShape("ellipse");
        other.addShape("rect");
        QVERIFY(other.setMetadata("Different released project", "Local fixture"));
        const auto otherFile = QUrl::fromLocalFile(storage.filePath("other.blazer"));
        QVERIFY(other.save(otherFile));
        const auto otherBytes = other.projectBytes();
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("deviceManager", &devices);
        context->setContextProperty("materialLibrary", &materials);
        context->setContextProperty("materialTestLedger", &ledger);
        context->setContextProperty("artLibrary", &arts);
        context->setContextProperty("artLibraryController", &artController);
        context->setContextProperty("vectorInspectionService", &vectors);
        context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(compactDark ? 1180 : 1480, compactDark ? 760 : 920);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](const QString &name) { return find(window->contentItem(), name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(name); };
        auto visible = [&](const char *name) {
            auto *value = object(name);
            return value && value->property("visible").toBool();
        };
        auto type = [&](QQuickItem *field, const QString &text) {
            if (!click(window, field))
                return false;
            field->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClick(window, Qt::Key_Backspace);
            for (char c : text.toLatin1())
                QTest::keyClick(window, c);
            return true;
        };
        auto fileResult = [&](const char *name, const QUrl &file) {
            auto *picker = object(name);
            return picker && picker->setProperty("selectedFile", file) &&
                   QMetaObject::invokeMethod(picker, "accepted") &&
                   QMetaObject::invokeMethod(picker, "reject");
        };
        auto read = [](const QUrl &url) {
            QFile file(url.toLocalFile());
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        auto clipboard = [] {
            QVariantMap result;
            const auto *mime = QGuiApplication::clipboard()->mimeData();
            for (const auto &format : mime->formats())
                result[format] = mime->data(format);
            return result;
        };
        QTRY_VERIFY(visible("startupSafetyDialog"));
        QVERIFY(click(window, item("acknowledgeStartupSafety")));
        QTRY_VERIFY(!visible("startupSafetyDialog"));
        window->setProperty("basic", false);
        QTRY_VERIFY(project.job().valid());
        auto *mime = new QMimeData;
        mime->setData("image/svg+xml",
                      "<svg xmlns='http://www.w3.org/2000/svg' width='5mm' height='5mm' viewBox='0 "
                      "0 5 5'><rect width='5' height='5'/></svg>");
        mime->setText("KEEP ARTWORK CLIPBOARD");
        QGuiApplication::clipboard()->setMimeData(mime);
        const auto preservedClipboard = clipboard();
        machine.start();
        QVERIFY(machine.busy());
        block(true);
        QTRY_VERIFY(!window->property("synchronousWorkReady").toBool());
        auto *status = item("synchronousWorkStatus");
        QVERIFY(status);
        QTRY_VERIFY(status->isVisible());
        QCOMPARE(status->property("text").toString(), guardReason);
        for (const auto &name : QStringList{
                 "openProjectButton", "saveProjectButton", "importMotifButton", "importImageButton",
                 "exportGcodeButton", "clipboardCopyButton", "clipboardPasteButton"}) {
            auto *button = item(name);
            QVERIFY2(button, qPrintable(name));
            QVERIFY2(!button->isEnabled(), qPrintable(name));
            QVERIFY2(click(window, button, false), qPrintable(name));
        }
        item("workspace")->forceActiveFocus();
        for (const auto key : {Qt::Key_O, Qt::Key_S, Qt::Key_I, Qt::Key_C, Qt::Key_X, Qt::Key_V})
            QTest::keyClick(window, key, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(50);
        QVERIFY(!visible("openProjectDialog"));
        QVERIFY(!visible("saveProjectDialog"));
        QVERIFY(!visible("importMotifDialog"));
        QVERIFY(!visible("exportGcodeDialog"));
        QVERIFY(!visible("discardDialog"));
        QCOMPARE(window->property("pendingAction").toString(), QString());
        QCOMPARE(clipboard(), preservedClipboard);
        QCOMPARE(project.count(), 1);
        const auto folder = qEnvironmentVariable("BLAZER_SYNC_UI_EVIDENCE");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QTest::qWait(60);
            QVERIFY(window->grabWindow().save(folder + "/work-gate-" + QTest::currentDataTag() +
                                              ".png"));
        }
        QVERIFY(machine.busy());
        QTest::keyClick(window, Qt::Key_Period, Qt::ControlModifier);
        QTRY_VERIFY(!machine.busy());
        QVERIFY(!machine.firing());
        QVERIFY(!window->property("synchronousWorkReady").toBool());
        block(false);
        QTRY_VERIFY(window->property("synchronousWorkReady").toBool());
        QTRY_VERIFY(!status->isVisible());
        QCOMPARE(project.projectBytes(),
                 initialBytes); // Inspect serialized payload only after releasing the guard.
        QVERIFY(item("openProjectButton")->isEnabled());
        QVERIFY(item("saveProjectButton")->isEnabled());
        QVERIFY(item("exportGcodeButton")->isEnabled());
        // Local text copy is still permitted; artwork serialization shortcuts remain blocked.
        block(true);
        auto *name = item("objectNameInput");
        QVERIFY(name);
        QVERIFY(name->isEnabled());
        QVERIFY(click(window, name));
        name->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("Original next design"));
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasFormat("image/svg+xml"));
        QVERIFY(type(name, "Allowed next design"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(project.selection()["name"].toString(), QString("Allowed next design"));
        auto *x = item("objectXInput")->findChild<QQuickItem *>("numericEditor");
        QVERIFY(x);
        QVERIFY(type(x, "12 mm"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(project.selection()["x"].toDouble(), 12.);
        block(false);
        const auto editedBytes = project.projectBytes();
        QVERIFY(editedBytes != initialBytes);
        const auto normalSave = QUrl::fromLocalFile(storage.filePath("released-save.blazer"));
        QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(visible("saveProjectDialog"));
        QVERIFY(fileResult("saveProjectDialog", normalSave));
        QCOMPARE(read(normalSave), editedBytes);
        QVERIFY(window->isVisible());
        QVERIFY(!visible("openProjectDialog"));
        // A close request whose Save is denied must never replay Quit on a later successful Save.
        project.setProperty("name", "Close request remains local");
        window->close();
        QTRY_VERIFY(visible("discardDialog"));
        QCOMPARE(window->property("pendingAction").toString(), QString("close"));
        block(true);
        QVERIFY(click(window, item("saveChangesButton")));
        QTRY_VERIFY(!visible("discardDialog"));
        QVERIFY(!visible("saveProjectDialog"));
        QCOMPARE(window->property("pendingAction").toString(), QString());
        QVERIFY(window->isVisible());
        block(false);
        const auto afterClose = QUrl::fromLocalFile(storage.filePath("after-denied-close.blazer"));
        QVERIFY(click(window, item("saveProjectButton")));
        QTRY_VERIFY(visible("saveProjectDialog"));
        QVERIFY(fileResult("saveProjectDialog", afterClose));
        QVERIFY(QFile::exists(afterClose.toLocalFile()));
        QTest::qWait(50);
        QVERIFY(window->isVisible());
        QVERIFY(!visible("openProjectDialog"));
        // Already-open Save may become blocked before acceptance; its pending Open must be
        // discarded.
        project.setProperty("name", "Open request remains local");
        QVERIFY(click(window, item("openProjectButton")));
        QTRY_VERIFY(visible("discardDialog"));
        QCOMPARE(window->property("pendingAction").toString(), QString("open"));
        QVERIFY(click(window, item("saveChangesButton")));
        QTRY_VERIFY(visible("saveProjectDialog"));
        block(true);
        const auto denied = QUrl::fromLocalFile(storage.filePath("denied-save.blazer"));
        QVERIFY(fileResult("saveProjectDialog", denied));
        QVERIFY(!QFile::exists(denied.toLocalFile()));
        QCOMPARE(window->property("pendingAction").toString(), QString());
        QVERIFY(!visible("openProjectDialog"));
        block(false);
        const auto finalBytes = project.projectBytes();
        const auto finalSave = QUrl::fromLocalFile(storage.filePath("after-denied-open.blazer"));
        QVERIFY(click(window, item("saveProjectButton")));
        QTRY_VERIFY(visible("saveProjectDialog"));
        QVERIFY(fileResult("saveProjectDialog", finalSave));
        QCOMPARE(read(finalSave), finalBytes);
        QTest::qWait(50);
        QVERIFY(window->isVisible());
        QVERIFY(!visible("openProjectDialog"));
        // Freely allowed shortcuts and GCode actions actually work again after release.
        item("workspace")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY(visible("openProjectDialog"));
        QVERIFY(fileResult("openProjectDialog", otherFile));
        QCOMPARE(project.projectBytes(), otherBytes);
        QCOMPARE(project.title(), QString("Different released project"));
        QCOMPARE(project.count(), 2);
        const auto svg = storage.filePath("local.svg");
        QFile svgFile(svg);
        QVERIFY(svgFile.open(QIODevice::WriteOnly));
        svgFile.write("<svg xmlns='http://www.w3.org/2000/svg' width='5mm' height='5mm' viewBox='0 "
                      "0 5 5'><rect width='5' height='5'/></svg>");
        svgFile.close();
        const int beforeImport = project.count();
        item("workspace")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
        QTRY_VERIFY(visible("importMotifDialog"));
        QVERIFY(fileResult("importMotifDialog", QUrl::fromLocalFile(svg)));
        QCOMPARE(project.count(), beforeImport + 1);
        item("workspace")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY(
            QGuiApplication::clipboard()->mimeData()->hasFormat("application/x-blazer-objects-v1"));
        const int beforePaste = project.count();
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        QTRY_COMPARE(project.count(), beforePaste + 1);
        QVERIFY(click(window, item("exportGcodeButton")));
        QTRY_VERIFY(visible("exportGcodeDialog"));
        const auto gcode = QUrl::fromLocalFile(storage.filePath("released.gcode"));
        QVERIFY(fileResult("exportGcodeDialog", gcode));
        QVERIFY(!read(gcode).isEmpty());
        QVERIFY(machine.simulation());
        QVERIFY(machine.ports().isEmpty());
        window->setProperty("discardCloseApproved", true);
        window->close();
    }
    void basicDirectStartThroughRealMain_data() {
        QTest::addColumn<bool>("compactDark");QTest::addColumn<bool>("fromFile");
        QTest::newRow("shape-normal")<<false<<false;QTest::newRow("shape-compact")<<true<<false;
        QTest::newRow("file-normal")<<false<<true;QTest::newRow("file-compact")<<true<<true;
    }
    void basicDirectStartThroughRealMain() {
        QFETCH(bool,compactDark);QFETCH(bool,fromFile);QTemporaryDir storage;QVERIFY(storage.isValid());
        Document project;project.setBackgroundPlanning(false);project.newProject();
        if(fromFile) {
            Document source;source.setBackgroundPlanning(false);source.newProject();
            QVERIFY(source.applyBasicMaterial(MaterialLibrary::example()));source.addShape("rect");
            source.setBasicOperation("outline");source.setProperty("width",100.);source.setProperty("height",50.);
            const auto file=QUrl::fromLocalFile(storage.filePath("direct-start.blazer"));QVERIFY(source.save(file));QVERIFY(project.open(file));
        } else QVERIFY(project.applyBasicMaterial(MaterialLibrary::example()));
        Machine machine(&project,nullptr,Machine::HardwareAccess::Disabled);
        DeviceManager devices(&project,&machine,storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));MaterialTestLedger ledger(storage.filePath("ledger.json"));
        ArtLibrary arts(storage.filePath("arts.json"));ArtLibraryController artController(&project,&machine,&arts);
        VectorInspectionService vectors(&project);Registration registration;
        ApplicationServices services(&project,&machine,storage.filePath("settings.ini"));QVERIFY(services.finishOnboarding());QVERIFY(services.setDarkTheme(compactDark));
        QQmlApplicationEngine engine;auto *context=engine.rootContext();
        context->setContextProperty("project",&project);context->setContextProperty("machine",&machine);
        context->setContextProperty("deviceManager",&devices);context->setContextProperty("materialLibrary",&materials);
        context->setContextProperty("materialTestLedger",&ledger);context->setContextProperty("artLibrary",&arts);
        context->setContextProperty("artLibraryController",&artController);context->setContextProperty("vectorInspectionService",&vectors);
        context->setContextProperty("cameraRegistration",&registration);context->setContextProperty("appServices",&services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR)+"/qml/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);window->resize(compactDark?1180:1480,compactDark?760:920);
        QVERIFY(QTest::qWaitForWindowExposed(window));auto item=[&](const QString&name){return find(window->contentItem(),name);};
        QTRY_VERIFY(window->findChild<QObject*>("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window,item("acknowledgeStartupSafety")));QTRY_VERIFY(!window->findChild<QObject*>("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(window->property("basic").toBool());QCOMPARE(window->property("page").toInt(),0);
        if(!fromFile){QVERIFY(click(window,item("addRectangle")));QCOMPARE(project.count(),1);project.setBasicOperation("outline");project.setProperty("width",100.);project.setProperty("height",50.);}
        QTRY_VERIFY(project.job().valid());QVERIFY(!machine.busy());
        // Optional explicit preview remains available in Basic.
        QVERIFY(click(window,item("workflow1")));QCOMPARE(window->property("page").toInt(),1);QVERIFY(!machine.busy());
        QVERIFY(click(window,item("workflow0")));QCOMPARE(window->property("page").toInt(),0);
        // Advanced keeps its established first-click Preview contract.
        QVERIFY(click(window,item("modeToggle")));QVERIFY(!window->property("basic").toBool());
        QVERIFY(click(window,item("startJobButton")));QCOMPARE(window->property("page").toInt(),1);QVERIFY(!machine.busy());
        QVERIFY(click(window,item("workflow0")));QVERIFY(click(window,item("modeToggle")));QVERIFY(window->property("basic").toBool());
        auto *start=item("startJobButton");QVERIFY(start);QTRY_VERIFY(start->isEnabled());
        QCOMPARE(start->property("nativeActionRole").toString(),QString("startAttended"));
        QSignalSpy clicked(start,SIGNAL(clicked()));
        QVERIFY(click(window,start));QCOMPARE(clicked.count(),1);
        QTRY_VERIFY(machine.busy());QCOMPARE(window->property("page").toInt(),2);
        QVERIFY(machine.simulation());QCOMPARE(machine.connectionPhase(),QString("closed"));QVERIFY(!machine.connectionVerified());
        QTRY_VERIFY(item("liveJobPanel")->isVisible());QVERIFY(!item("deviceSetupScroll")->isVisible());
        auto *liveWorkspace=item("liveJobWorkspace");QVERIFY(liveWorkspace);QVERIFY(liveWorkspace->property("liveJobView").toBool());
        QTRY_VERIFY(liveWorkspace->height()>=280);QVERIFY(liveWorkspace->width()>200);
        QVERIFY(!item("basicGuidePanel")->isVisible());
        const auto live=[&]{return machine.property("liveJob").toMap();};
        QTRY_VERIFY(live().value("valid").toBool());
        QTRY_VERIFY(live().value("visualizationReady").toBool());
        QTRY_VERIFY(!live().value("sourceFingerprint").toString().isEmpty());
        QTRY_VERIFY(window->property("livePercent").toDouble()>=3.);
        QVERIFY(item("liveJobPercent")->property("text").toString().contains('%'));
        const auto source=live().value("sourceFingerprint");QVERIFY(source.isValid());QVERIFY(!source.toString().isEmpty());
        auto *pause=item("pauseJobButton");auto *stop=item("stopJobButton");QVERIFY(pause&&stop);
        QVERIFY(pause->isVisible()&&stop->isVisible());QVERIFY(pause->isEnabled());
        QCOMPARE(window->findChildren<QObject*>("pauseJobButton").size(),1);
        QCOMPARE(window->findChildren<QObject*>("stopJobButton").size(),1);
        QCOMPARE(window->findChildren<QObject*>("frameJobButton").size(),1);
        QVERIFY(click(window,pause));QTRY_VERIFY(machine.paused());
        const auto heldPercent=live().value("percent");QTest::qWait(140);QCOMPARE(live().value("percent"),heldPercent);
        QTRY_COMPARE(live().value("phase").toString(),QString("paused"));
        QTRY_VERIFY(item("liveJobPhase")->property("text").toString().contains("Pausiert"));
        const auto screenshots=qEnvironmentVariable("BLAZER_LIVE_PREVIEW_SCREENSHOT_DIR");
        if(!screenshots.isEmpty()) {
            QVERIFY(QDir().mkpath(screenshots));QTest::qWait(60);
            const auto image=window->grabWindow();QVERIFY(!image.isNull());
            QVERIFY(image.save(screenshots+"/"+QTest::currentDataTag()+"-paused.png"));
        }
        project.setProperty("x",70.);QCOMPARE(live().value("sourceFingerprint"),source);QVERIFY(live().value("visualizationReady").toBool());
        QVERIFY(click(window,pause));QTRY_VERIFY(machine.running());
        QVERIFY(click(window,item("liveDeviceDetailsButton")));QTRY_VERIFY(item("deviceSetupScroll")->isVisible());QTRY_VERIFY(item("basicGuidePanel")->isVisible());
        QVERIFY(pause->isVisible()&&stop->isVisible());
        QVERIFY(click(window,item("showLiveJobButton")));QTRY_VERIFY(item("liveJobPanel")->isVisible());
        QVERIFY(click(window,stop));QTRY_VERIFY(!machine.busy());QVERIFY(!machine.firing());
        QVERIFY(item("stopJobButton")->isVisible());
        QTRY_VERIFY(item("frameJobButton")->isEnabled());QVERIFY(click(window,item("frameJobButton")));
        QVERIFY(!window->property("livePreviewSelected").toBool());QTRY_VERIFY(item("deviceSetupScroll")->isVisible());
        QVERIFY(click(window,item("globalStopButton")));QTRY_VERIFY(!machine.busy());QVERIFY(!machine.firing());
        window->setProperty("discardCloseApproved",true);window->close();
    }
    void attendedReadinessThroughRealMain() {
        QTemporaryDir storage;QVERIFY(storage.isValid());Document project;project.setBackgroundPlanning(false);project.newProject();
        QVERIFY(project.applyBasicMaterial(MaterialLibrary::example()));project.addShape("rect");project.setBasicOperation("outline");
        AttendedReadinessMachine machine(&project);DeviceManager devices(&project,&machine,storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));MaterialTestLedger ledger(storage.filePath("ledger.json"));
        ArtLibrary arts(storage.filePath("arts.json"));ArtLibraryController artController(&project,&machine,&arts);
        VectorInspectionService vectors(&project);Registration registration;ApplicationServices services(&project,&machine,storage.filePath("settings.ini"));QVERIFY(services.finishOnboarding());
        QQmlApplicationEngine engine;auto *context=engine.rootContext();
        context->setContextProperty("project",&project);context->setContextProperty("machine",&machine);
        context->setContextProperty("deviceManager",&devices);context->setContextProperty("materialLibrary",&materials);
        context->setContextProperty("materialTestLedger",&ledger);context->setContextProperty("artLibrary",&arts);
        context->setContextProperty("artLibraryController",&artController);context->setContextProperty("vectorInspectionService",&vectors);
        context->setContextProperty("cameraRegistration",&registration);context->setContextProperty("appServices",&services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR)+"/qml/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item=[&](const QString&name){return find(window->contentItem(),name);};
        QTRY_VERIFY(window->findChild<QObject*>("startupSafetyDialog")->property("visible").toBool());QVERIFY(click(window,item("acknowledgeStartupSafety")));
        QTRY_VERIFY(window->property("useAttendedStart").toBool());
        // Deliberately no pendingAction or old separate material/presence proof.
        QVERIFY(!window->property("proofConfirmed").toBool());
        QTRY_VERIFY2(window->property("startBlockReason").toString().isEmpty(),qPrintable(window->property("startBlockReason").toString()));
        QTRY_VERIFY(item("startJobButton")->isEnabled());
        machine.setAttendedError("ATTENDED_INTERLOCK_DENIED");QTRY_COMPARE(window->property("startBlockReason").toString(),QString("ATTENDED_INTERLOCK_DENIED"));
        QVERIFY(!item("startJobButton")->isEnabled());QVERIFY(!machine.busy());
        machine.setAttendedError({});QTRY_VERIFY(window->property("startBlockReason").toString().isEmpty());
        window->setProperty("page",2);
        auto *reference=item("referenceHomeButton");QVERIFY(reference);
        QTRY_VERIFY(reference->isVisible()&&reference->isEnabled());
        QCOMPARE(reference->property("nativeActionRole").toString(),QString("referenceHome"));
        const auto referenceCenter=reference->mapToScene(reference->boundingRect().center());
        QVERIFY(QRectF(0,0,window->width(),window->height()).contains(referenceCenter));
        QVERIFY(window->property("basic").toBool());
        project.setProperty("x",250.);QTRY_VERIFY(!project.job().valid());
        QTRY_VERIFY(!window->property("startBlockReason").toString().isEmpty());QVERIFY(!item("startJobButton")->isEnabled());
        QVERIFY(!machine.busy());QVERIFY(!machine.firing());
        window->setProperty("page",2);
        machine.setLiveState({{"valid",true},{"jobId",1},{"sourceFingerprint","fixed-job"},{"active",true},{"completed",false},{"phase","parking"},{"percent",100.},{"estimateKnown",true},{"headFresh",false},{"visualizationReady",false}});
        QTRY_VERIFY(item("liveJobPanel")->isVisible());QCOMPARE(window->property("livePercent").toDouble(),99.);
        QVERIFY(item("liveJobPhase")->property("text").toString().contains("Rückfahrt"));QVERIFY(item("liveJobVisualizationPending")->isVisible());
        QVERIFY(item("liveJobPositionStatus")->isVisible());QCOMPARE(item("liveJobPositionStatus")->property("text").toString(),QString("Position nicht aktuell"));
        QVERIFY(item("stopJobButton")->isVisible());
        machine.setLiveState({{"valid",true},{"jobId",1},{"sourceFingerprint","fixed-job"},{"active",false},{"completed",true},{"phase","complete"},{"percent",100.},{"estimateKnown",true},{"headFresh",true}});
        QTRY_COMPARE(window->property("livePercent").toDouble(),100.);QVERIFY(item("liveJobPanel")->isVisible());
        QVERIFY(!item("liveJobPositionStatus")->isVisible());
        // Confirmed completion is100% even when no duration estimate existed.
        machine.setLiveState({{"valid",true},{"jobId",1},{"sourceFingerprint","fixed-job"},{"active",false},{"completed",true},{"phase","complete"},{"percent",100.},{"estimateKnown",false},{"headFresh",true}});
        QTRY_COMPARE(window->property("livePercent").toDouble(),100.);
        QVERIFY(item("liveJobPercent")->property("text").toString().contains("100 %"));
        QVERIFY(click(window,item("liveDeviceDetailsButton")));QVERIFY(!window->property("livePreviewSelected").toBool());
        machine.setLiveState({{"valid",true},{"jobId",1},{"sourceFingerprint","image-ready-source"},{"active",false},{"completed",true},{"phase","complete"},{"percent",100.},{"estimateKnown",true},{"headFresh",false}});
        QTRY_VERIFY(!item("liveJobPanel")->isVisible());
        // A genuinely new job restores Live even when geometry is identical;
        // mere status/freshness changes of the old job preserve the user's view.
        machine.setLiveState({{"valid",true},{"jobId",2},{"sourceFingerprint","fixed-job"},{"active",true},{"completed",false},{"phase","engraving"},{"percent",1.},{"estimateKnown",true},{"headFresh",true}});
        QTRY_VERIFY(item("liveJobPanel")->isVisible());
        machine.setLiveState({{"valid",true},{"jobId",2},{"sourceFingerprint","fixed-job"},{"active",false},{"completed",false},{"phase","fault"},{"percent",42.},{"estimateKnown",true},{"headFresh",false}});
        QTRY_COMPARE(window->property("livePercent").toDouble(),42.);QVERIFY(item("liveJobPhase")->property("text").toString().contains("abgebrochen"));
        QVERIFY(item("stopJobButton")->isVisible());QVERIFY(!machine.busy());QVERIFY(!machine.firing());
        window->setProperty("discardCloseApproved",true);window->close();
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
        ArtLibrary artLibrary(storage.filePath("art-library.blazerart"));
        ArtLibraryController artController(&project,&machine,&artLibrary);
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
        context->setContextProperty("artLibrary", &artLibrary);
        context->setContextProperty("artLibraryController", &artController);
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
        QVERIFY(press("modeToggle"));QVERIFY(!window->property("basic").toBool());

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

        QVERIFY(press("modeToggle"));QVERIFY(window->property("basic").toBool());
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

        QVERIFY(press("betaHelpButton")); QVERIFY(press("exportDiagnosticsButton"));
        QTRY_VERIFY(object("technicalDiagnosticsPreviewDialog")->property("visible").toBool());
        auto *previewText = item("technicalDiagnosticsPreviewText"); QVERIFY(previewText);
        QVERIFY(!previewText->property("text").toString().contains("controllerContext"));
        QVERIFY(press("includeControllerDiagnostics"));
        QTRY_VERIFY(previewText->property("text").toString().contains("controllerContext"));
        QVERIFY(previewText->property("text").toString().contains("simulator"));
        QVERIFY(!previewText->property("text").toString().contains("/dev/never-open"));
        const auto frozenDiagnosticBytes=previewText->property("text").toString().toUtf8();
        QVERIFY(picture("technical-diagnostics")); QVERIFY(press("saveTechnicalDiagnostics"));
        const auto diagnosisPath=storage.filePath("reviewed-diagnostic.json");
        QVERIFY(fileResult("technicalDiagnosticsFileDialog",diagnosisPath));
        QFile diagnosisFile(diagnosisPath); QVERIFY(diagnosisFile.open(QIODevice::ReadOnly));
        QCOMPARE(diagnosisFile.readAll(),frozenDiagnosticBytes);
        QTRY_VERIFY(!object("technicalDiagnosticsPreviewDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(object("betaHelpDialog"),"close"));

        window->setProperty("page",2);
        QTRY_VERIFY(item("frameJobButton")->isEnabled());
        QVERIFY(press("frameJobButton")); QTRY_VERIFY(machine.busy()); QVERIFY(!machine.firing());
        QVERIFY(window->property("basic").toBool());
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
