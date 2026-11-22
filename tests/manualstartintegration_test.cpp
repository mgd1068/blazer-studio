#include "controllerruntime.h"
#include "document.h"
#include "machine.h"
#include "nativeinputintent.h"
#include "support/runtimepeer.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <functional>
#include <memory>
using RuntimeTestSupport::RuntimePeer;

// TEST ONLY: accept a deliberately synthetic activation on the actual registered
// QML control. Never replace its registration/binding provider with a carrier,
// change production grants, or claim OS/human input or physical measurement.
class NativeInputIntentTestAccess {
  public:
    static bool seedExisting(NativeInputIntentAdapter &adapter, QQuickItem &control,
                             const QString &action, const QByteArray &binding) {
        const auto key = action + "#" + QString::number(quintptr(&control), 16);
        if (binding.isEmpty() || adapter.m_controls.value(key) != &control ||
            !adapter.m_clickBindings.value(key) || adapter.m_clickBindings.value(key)() != binding)
            return false;
        const auto now = NativeInputIntentAdapter::nowMs();
        adapter.m_domain = NativeInputIntentAdapter::Domain::X11Server32;
        adapter.m_sample = {1000, now - 5, now - 4};
        auto token = std::make_shared<NativeInputContextToken>();
        adapter.m_tokens[key] = token;
        adapter.m_pending[key] = {{++adapter.m_interaction, now, now + 250, binding, token},
                                  &control,
                                  control.window(),
                                  adapter.m_context,
                                  false,
                                  true};
        return true;
    }
};
class ControllerRuntimeTestAccess {
  public:
    static bool seed(Machine &machine, QQuickItem &control, const QString &action) {
        if (!machine.session)
            return false;
        const auto binding = machine.nativeBinding(action, {}, machine.session->status());
        if (machine.nativeArguments(&control, {}) != QVariantMap{})
            return false;
        return NativeInputIntentTestAccess::seedExisting(*machine.nativeInput, control, action,
                                                         binding);
    }
};
namespace {
QQuickItem *find(QQuickItem *item, const QString &name) {
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *match = find(child, name))
            return match;
    return nullptr;
}
bool reveal(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible())
        return false;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 1000))
        return false;
    QElapsedTimer budget;
    budget.start();
    do {
        QTest::qWait(20);
        for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->property("contentY").isValid())
                continue;
            const auto point = item->mapToItem(parent, QPointF());
            const auto delta = point.y() < 5 ? point.y() - 5
                               : point.y() + item->height() > parent->height() - 5
                                   ? point.y() + item->height() - parent->height() + 5
                                   : 0;
            parent->setProperty(
                "contentY", std::clamp(parent->property("contentY").toDouble() + delta, 0.,
                                       std::max(0., parent->property("contentHeight").toDouble() -
                                                        parent->height())));
        }
        QTest::qWait(20);
        const auto center = item->boundingRect().center();
        bool visible =
            QRectF(0, 0, window->width(), window->height()).contains(item->mapToScene(center));
        for (auto *parent = item->parentItem(); visible && parent; parent = parent->parentItem())
            if (parent->clip())
                visible = QRectF(0, 0, parent->width(), parent->height())
                              .contains(item->mapToItem(parent, center));
        if (visible)
            return true;
    } while (budget.elapsed() < 1200);
    return false;
}
// Application-wide production filters reject the QTest event as non-native.
// This window-only test filter then installs the explicit synthetic proof just
// before QtQuick processes the real release and emits the button's clicked.
class ProofOnRelease final : public QObject {
  public:
    std::function<bool()> seed;
    bool attempted = false, seeded = false;
    bool eventFilter(QObject *, QEvent *event) override {
        if (!attempted && event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            attempted = true;
            seeded = seed();
        }
        return false;
    }
};
bool click(QQuickWindow *window, QQuickItem *control, Machine *machine = nullptr,
           const QString &action = {}) {
    if (!control || !control->isEnabled() || !reveal(window, control)) {
        qInfo() << "Click precondition" << (control ? control->objectName() : QString("missing"))
                << (control ? control->isVisible() : false)
                << (control ? control->isEnabled() : false);
        if (machine)
            qInfo() << "Native precondition state" << machine->operatorLocalFrame()
                    << machine->requestPending() << machine->busy();
        return false;
    }
    ProofOnRelease proof;
    if (machine) {
        proof.seed = [&] { return ControllerRuntimeTestAccess::seed(*machine, *control, action); };
        window->installEventFilter(&proof);
    }
    QSignalSpy clicked(control, SIGNAL(clicked()));
    const auto point = control->mapToScene(control->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, {}, point);
    window->removeEventFilter(&proof);
    const bool delivered = clicked.isValid() && clicked.count() == 1 &&
                           (!machine || (proof.attempted && proof.seeded));
    if (!delivered) {
        qInfo() << "Click delivery" << control->objectName() << clicked.count() << proof.attempted
                << proof.seeded << control->isEnabled();
        if (machine)
            qInfo() << "Native delivery state" << machine->operatorLocalFrame()
                    << machine->requestPending() << machine->busy();
    }
    return delivered;
}
bool type(QQuickWindow *window, QQuickItem *control, const QString &text) {
    if (!control || !control->isEnabled() || !reveal(window, control))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {},
                      control->mapToScene(control->boundingRect().center()).toPoint());
    control->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (char c : text.toLatin1())
        QTest::keyClick(window, c);
    return true;
}
int count(RuntimePeer &peer, const QString &kind) {
    return peer.snapshot().object.value("counts").toObject().value(kind).toInt();
}
QString phase(Machine &machine) {
    return machine.operatorLocalFrame().value("phase").toString();
}
} // namespace
class ManualStartIntegrationTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QQuickStyle::setStyle("Basic"); }
    void actualDialogCaptureFrameStartOnOwnedPlant() {
        RuntimePeer peer;
        QVERIFY2(peer.start(QStringLiteral(BLAZER_TEST_SOURCE_DIR
                                           "/tests/support/m3_commissioning_peer.py"))
                     .ok,
                 qPrintable(peer.diagnostic()));
        Document project;
        project.setBackgroundPlanning(false);
        project.newProject();
        project.addShape("rect");
        project.setProperty("x", 5.);
        project.setProperty("y", 5.);
        project.setProperty("width", 5.);
        project.setProperty("height", 5.);
        QVERIFY(project.setStartMode(Document::CurrentPosition));
        Machine machine(&project);
        QVERIFY(machine.requestConnectHardware(peer.port(), QByteArray(64, 'a')).enqueued);
        QTRY_VERIFY(machine.ready());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("actualMachine", &machine);
        engine.rootContext()->setContextProperty("actualProject", &project);
        QString dir = qEnvironmentVariable("BLAZER_MANUALSTART_INTEGRATION_QML_DIR");
        if (dir.isEmpty())
            dir = QStringLiteral(BLAZER_TEST_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        component.setData(QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {
 width:1200; height:1000; visible:true
 property alias setup:setup
 App.ManualStartDialog {id:setup;modelMachine:actualMachine;modelProject:actualProject}
})")
                              .arg(QUrl::fromLocalFile(dir).toString())
                              .toUtf8(),
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *dialog = root->property("setup").value<QObject *>();
        QVERIFY(dialog);
        auto item = [&](const QString &name) { return find(window->contentItem(), name); };
        auto press = [&](const QString &name) { return click(window, item(name)); };
        auto native = [&](const QString &name, const QString &action) {
            return click(window, item(name), &machine, action);
        };
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QCOMPARE(count(peer, "physicalMove"), 0);
        QCOMPARE(count(peer, "powered"), 0);
        QCOMPARE(count(peer, "homed"), 0);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(press("beginManualPlacement"));
        QTRY_COMPARE(phase(machine), QString("positioning"));
        QCOMPARE(count(peer, "physicalMove"), 0);
        QCOMPARE(count(peer, "powered"), 0);
        // Move only the independent plant's actual head, preserving its counters.
        QVERIFY(peer.control({{"action", "plantConfig"},
                              {"manualPosition", QJsonArray{-45., -60.}},
                              {"doorSubstate", 1}})
                    .ok);
        QVERIFY(peer.control({{"action", "report"}}).ok);
        QTRY_COMPARE(machine.operatorLocalFrame().value("doorSubstate").toInt(), 1);
        QVERIFY(peer.control({{"action", "plantConfig"}, {"doorSubstate", 0}}).ok);
        QVERIFY(peer.control({{"action", "report"}}).ok);
        QTRY_COMPARE(machine.operatorLocalFrame().value("doorSubstate").toInt(), 0);
        const QStringList fields = {"manualNegativeX", "manualPositiveX", "manualNegativeY",
                                    "manualPositiveY"};
        const QStringList values = {"80", "40", "60", "50"};
        for (int n = 0; n < 4; ++n)
            QVERIFY(type(window, item(fields[n]), values[n]));
        QVERIFY(press("setManualTravel"));
        QTRY_COMPARE(
            machine.operatorLocalFrame().value("available").toMap().value("negativeX").toDouble(),
            80.);
        for (const auto &name : {"manualMotorsFreeWitness", "manualMeasuredTravelWitness",
                                 "manualHeadStationaryWitness", "manualCoverClosedWitness"})
            QVERIFY(press(name));
        QTRY_VERIFY(item("attestManualPlacement")->isEnabled());
        QVERIFY(press("attestManualPlacement"));
        QTRY_COMPARE(phase(machine), QString("captureReady"));
        QCOMPARE(count(peer, "physicalMove"), 0);
        QCOMPARE(count(peer, "powered"), 0);
        QTRY_VERIFY(item("captureOperatorLocalFrame")->isEnabled());
        QVERIFY(native("captureOperatorLocalFrame", "localCapture"));
        QTRY_VERIFY2(machine.operatorLocalFrame().value("localPositionKnown").toBool(),
                     qPrintable(machine.operatorLocalFrame().value("reason").toString()));
        QTRY_COMPARE(phase(machine), QString("travelReview"));
        QCOMPARE(count(peer, "physicalMove"), 0);
        QCOMPARE(count(peer, "powered"), 0);
        QCOMPARE(count(peer, "homed"), 0);
        QCOMPARE(peer.snapshot().object.value("actual").toArray(), QJsonArray({-45., -60.}));
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!machine.motionSafetyError().isEmpty());
        QVERIFY(machine.operatorLocalFrame().value("fits").toBool());
        QVERIFY(machine.operatorLocalFrame().value("frameReady").toBool());
        QTRY_VERIFY(item("frameLocalJob")->isEnabled());
        QVERIFY(native("frameLocalJob", "localFrame"));
        QTRY_VERIFY(count(peer, "physicalMove") > 0);
        QTRY_COMPARE(phase(machine), QString("travelReview"));
        QTRY_VERIFY(!machine.busy() && !machine.requestPending());
        QCOMPARE(count(peer, "powered"), 0);
        QCOMPARE(peer.snapshot().object.value("actual").toArray(), QJsonArray({-45., -60.}));
        const auto framedMoves = count(peer, "physicalMove");
        // A worker phase is not yet the QML eligibility notification. Wait for
        // the actual control to display readiness before simulating its click.
        QTRY_VERIFY(item("approveLocalTravel")->isEnabled());
        QVERIFY(press("approveLocalTravel"));
        QTRY_COMPARE(phase(machine), QString("ready"));
        QCOMPARE(count(peer, "physicalMove"), framedMoves);
        QCOMPARE(count(peer, "powered"), 0);
        QVERIFY(press("extractionClearanceWitness"));
        QVERIFY(press("confirmExtractionClearance"));
        QTRY_VERIFY(machine.extractionSupervision().contains("clearanceRequired") &&
                    !machine.extractionSupervision().value("clearanceRequired").toBool());
        QTRY_VERIFY(item("selectSupervisedDeviceCycle")->isEnabled());
        QVERIFY(press("selectSupervisedDeviceCycle"));
        QTRY_COMPARE(machine.extractionSupervision().value("mode").toString(),
                     QString("operatorSupervisedDeviceCycle"));
        QVERIFY(machine.extractionSupervision().value("presenceOnly").toBool());
        QVERIFY(!machine.extractionSupervision().value("requiresExternalExtraction").toBool());
        const auto fans = machine.extractionSupervision().value("deviceFans").toList();
        QCOMPARE(fans.size(), 2);
        for (const auto &value : fans) {
            const auto fan = value.toMap();
            QCOMPARE(fan.value("state").toString(), QString("unknown"));
            QCOMPARE(fan.value("feedback").toString(), QString("notReadBack"));
            QVERIFY(!fan.value("independentControl").toBool());
        }
        QVERIFY(!item("externalExtractionOnWitness")->isVisible());
        QVERIFY(press("deviceOperatorPresentWitness"));
        QTRY_VERIFY(item("attestDeviceCycle")->isEnabled());
        QVERIFY(press("attestDeviceCycle"));
        QTRY_VERIFY(machine.operatorLocalFrame().value("startReady").toBool());
        QCOMPARE(count(peer, "powered"), 0);
        QCOMPARE(count(peer, "physicalMove"), framedMoves);
        QTRY_VERIFY(item("startLocalJob")->isEnabled());
        QVERIFY(native("startLocalJob", "localStart"));
        QTRY_VERIFY(count(peer, "powered") > 0);
        QTRY_VERIFY(count(peer, "physicalMove") > framedMoves);
        QTRY_VERIFY(!machine.busy() && !machine.requestPending());
        QCOMPARE(count(peer, "collision"), 0);
        QCOMPARE(count(peer, "homed"), 0);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(press("stopManualStart"));
        QTRY_COMPARE(phase(machine), QString("invalid"));
        QVERIFY(!machine.operatorLocalFrame().value("localPositionKnown").toBool());
        machine.useSimulator();
        QTRY_VERIFY(machine.simulation() && machine.connectionPhase() == "closed");
    }
};
QTEST_MAIN(ManualStartIntegrationTest)
#include "manualstartintegration_test.moc"
