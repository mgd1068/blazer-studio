#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <algorithm>
#include <memory>
// Legacy model intentionally has no supervised-device-cycle API.
class LegacyDeviceModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap extractionSupervision READ extractionSupervision NOTIFY changed)
    Q_PROPERTY(bool requestPending MEMBER pending NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool firing MEMBER firing NOTIFY changed)
    Q_PROPERTY(bool paused MEMBER paused NOTIFY changed)
  public:
    bool pending = false, busy = false, firing = false, paused = false;
    int externalSelections = 0, externalAttests = 0, outputs = 0;
    QVariantMap state{{"mode", "sensorVerified"},
                      {"notice", "Gerätelüfter nicht rücklesbar"},
                      {"reason", ""},
                      {"activeLease", false},
                      {"pendingAction", ""},
                      {"remainingMs", 0}};
    QVariantMap extractionSupervision() const { return state; }
    void update() { emit changed(); }
    Q_INVOKABLE void confirmExtractionClearance(bool) {}
    Q_INVOKABLE void selectAttendedExtraction() {
        ++externalSelections;
        state["mode"] = "operatorAttendedExternal";
        emit changed();
    }
    Q_INVOKABLE void attestExternalExtraction(const QString &, bool, bool) { ++externalAttests; }
    Q_INVOKABLE void revokeExternalAttendance() {}
    Q_INVOKABLE void start() { ++outputs; }
    Q_INVOKABLE void resume() { ++outputs; }
    Q_INVOKABLE void focus() { ++outputs; }
  signals:
    void changed();
};
class DeviceCycleModel : public LegacyDeviceModel {
    Q_OBJECT
  public:
    int selections = 0, revocations = 0;
    QStringList attests;
    Q_INVOKABLE void selectSupervisedDeviceCycle() {
        ++selections;
        state["mode"] = "operatorSupervisedDeviceCycle";
        state["presenceOnly"] = true;
        state["requiresExternalExtraction"] = false;
        emit changed();
    }
    Q_INVOKABLE void attestDeviceCycle(const QString &action, bool present) {
        if (present) {
            attests.append(action);
            state["pendingAction"] = action;
            state["remainingMs"] = 30000;
            emit changed();
        }
    }
    Q_INVOKABLE void revokeDeviceAttendance() {
        ++revocations;
        state["activeLease"] = false;
        state["pendingAction"] = "";
        emit changed();
    }
};
namespace {
QQuickItem *find(QQuickItem *item, const QString &name) {
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *result = find(child, name))
            return result;
    return nullptr;
}
bool reveal(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible())
        return false;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 1000))
        return false;
    QElapsedTimer deadline;
    deadline.start();
    do {
        QTest::qWait(20);
        for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->property("contentY").isValid())
                continue;
            const auto p = item->mapToItem(parent, QPointF());
            const double delta = p.y() < 5 ? p.y() - 5
                                 : p.y() + item->height() > parent->height() - 5
                                     ? p.y() + item->height() - parent->height() + 5
                                     : 0;
            parent->setProperty(
                "contentY", std::clamp(parent->property("contentY").toDouble() + delta, 0.,
                                       std::max(0., parent->property("contentHeight").toDouble() -
                                                        parent->height())));
        }
        QTest::qWait(20);
        const auto center = item->boundingRect().center();
        bool okay =
            QRectF(0, 0, window->width(), window->height()).contains(item->mapToScene(center));
        for (auto *parent = item->parentItem(); okay && parent; parent = parent->parentItem())
            if (parent->clip())
                okay = QRectF(0, 0, parent->width(), parent->height())
                           .contains(item->mapToItem(parent, center));
        if (okay)
            return true;
    } while (deadline.elapsed() < 1200);
    return false;
}
bool click(QQuickWindow *window, QQuickItem *item, bool requireEnabled = true) {
    if (!item || (requireEnabled && !item->isEnabled()) || !reveal(window, item))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {},
                      item->mapToScene(item->boundingRect().center()).toPoint());
    return true;
}
struct Harness {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow *window = nullptr;
    QQuickItem *panel = nullptr;
    QString error;
    bool create(QObject *model, bool compact) {
        engine.rootContext()->setContextProperty("cycleModel", model);
        QString dir = qEnvironmentVariable("BLAZER_DEVICECYCLE_QML_DIR");
        if (dir.isEmpty())
            dir = QStringLiteral(BLAZER_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        component.setData(QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {width:%2;height:%3;visible:true
 ScrollView {anchors.fill:parent;anchors.margins:12;clip:true;contentWidth:availableWidth
 App.ExtractionSupervisionPanel {width:parent.width;modelMachine:cycleModel;dark:%4}}
})")
                              .arg(QUrl::fromLocalFile(dir).toString())
                              .arg(compact ? 480 : 900)
                              .arg(compact ? 520 : 900)
                              .arg(compact ? "true" : "false")
                              .toUtf8(),
                          QUrl());
        root.reset(component.create());
        error = component.errorString();
        if (!root)
            return false;
        window = qobject_cast<QQuickWindow *>(root.get());
        if (!window)
            return false;
        panel = find(window->contentItem(), "extractionSupervisionPanel");
        return panel && QTest::qWaitForWindowExposed(window, 1000);
    }
    QQuickItem *item(const QString &name) { return find(window->contentItem(), name); }
    bool press(const QString &name, bool enabled = true) {
        return click(window, item(name), enabled);
    }
    bool action(int index) {
        auto *choice = item("deviceCycleActionChoice");
        if (!click(window, choice))
            return false;
        QTest::keyClick(window, Qt::Key_Home);
        for (int n = 0; n < index; ++n)
            QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        return choice->property("currentIndex").toInt() == index;
    }
};
} // namespace
class DeviceCycleUiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QQuickStyle::setStyle("Basic"); }
    void normalDeviceWorkflow_data() {
        QTest::addColumn<bool>("compact");
        QTest::newRow("normal-light") << false;
        QTest::newRow("compact-dark") << true;
    }
    void normalDeviceWorkflow() {
        QFETCH(bool, compact);
        DeviceCycleModel model;
        Harness h;
        QVERIFY2(h.create(&model, compact), qPrintable(h.error));
        QCOMPARE(model.selections, 0);
        QCOMPARE(model.outputs, 0);
        QVERIFY(!h.item("selectSupervisedDeviceCycle")->isEnabled());
        QVERIFY(!h.item("selectAttendedExtraction")->isVisible());
        QVERIFY(!h.item("externalExtractionOnWitness")->isVisible());
        h.panel->setProperty("setupReady", true);
        QVERIFY(h.press("selectSupervisedDeviceCycle"));
        QCOMPARE(model.selections, 1);
        QVERIFY(h.item("deviceEnclosureFanStatus")->isVisible());
        QVERIFY(h.item("deviceLaserFanStatus")->isVisible());
        for (const auto &name : {"deviceEnclosureFanStatus", "deviceLaserFanStatus"}) {
            const auto text = h.item(name)->property("text").toString();
            QVERIFY(text.contains("nicht vom Controller zurückgemeldet"));
            QCOMPARE(h.item(name)->property("textFormat").toInt(), int(Qt::PlainText));
        }
        QVERIFY(!h.item("selectAttendedExtraction")->isVisible());
        for (int index = 0; index < 3; ++index) {
            model.paused = index == 2;
            model.busy = index == 2;
            model.update();
            QVERIFY(h.action(index));
            model.state["remainingMs"] = 12345;
            model.update();
            QCOMPARE(h.item("deviceCycleActionChoice")->property("currentIndex").toInt(), index);
            QVERIFY(!h.item("attestDeviceCycle")->isEnabled());
            QVERIFY(h.press("deviceOperatorPresentWitness"));
            QVERIFY(h.press("attestDeviceCycle"));
            QCOMPARE(model.attests.last(), QStringList({"start", "focus", "resume"})[index]);
            QVERIFY(!h.item("deviceOperatorPresentWitness")->property("checked").toBool());
            QCOMPARE(model.outputs, 0);
            QCOMPARE(model.externalAttests, 0);
            QCOMPARE(model.externalSelections, 0);
        }
        model.pending = true;
        model.firing = true;
        model.busy = true;
        model.state["activeLease"] = true;
        model.update();
        QVERIFY(h.press("revokeDeviceAttendance"));
        QCOMPARE(model.revocations, 1);
        QCOMPARE(model.outputs, 0);
        QCOMPARE(model.externalAttests, 0);
    }
    void externalOptionsAreExplicit() {
        DeviceCycleModel model;
        Harness h;
        QVERIFY2(h.create(&model, true), qPrintable(h.error));
        h.panel->setProperty("setupReady", true);
        QVERIFY(h.press("selectSupervisedDeviceCycle"));
        QVERIFY(!h.item("selectAttendedExtraction")->isVisible());
        QVERIFY(h.press("showExternalExtractionOptions"));
        QVERIFY(h.item("selectAttendedExtraction")->isVisible());
        QCOMPARE(model.externalSelections, 0);
        QVERIFY(h.press("selectAttendedExtraction"));
        QCOMPARE(model.externalSelections, 1);
        QCOMPARE(model.externalAttests, 0);
        QCOMPARE(model.outputs, 0);
    }
    void legacyModelKeepsExistingExternalWorkflow() {
        LegacyDeviceModel model;
        Harness h;
        QVERIFY2(h.create(&model, false), qPrintable(h.error));
        h.panel->setProperty("setupReady", true);
        QVERIFY(!h.panel->property("deviceCycleSupported").toBool());
        QVERIFY(!h.item("selectSupervisedDeviceCycle")->isVisible());
        QVERIFY(h.item("selectAttendedExtraction")->isVisible());
        QVERIFY(h.press("selectAttendedExtraction"));
        QCOMPARE(model.externalSelections, 1);
        QCOMPARE(model.outputs, 0);
    }
    void pendingAndFiringBlockAttestation_data() {
        QTest::addColumn<bool>("pending");
        QTest::newRow("pending") << true;
        QTest::newRow("firing") << false;
    }
    void pendingAndFiringBlockAttestation() {
        QFETCH(bool, pending);
        DeviceCycleModel model;
        Harness h;
        QVERIFY2(h.create(&model, true), qPrintable(h.error));
        h.panel->setProperty("setupReady", true);
        QVERIFY(h.press("selectSupervisedDeviceCycle"));
        QVERIFY(h.press("deviceOperatorPresentWitness"));
        model.pending = pending;
        model.firing = !pending;
        model.update();
        QVERIFY(!h.item("attestDeviceCycle")->isEnabled());
        QVERIFY(h.press("attestDeviceCycle", false));
        QVERIFY(model.attests.isEmpty());
        QVERIFY(h.press("revokeDeviceAttendance"));
        QCOMPARE(model.revocations, 1);
        QCOMPARE(model.outputs, 0);
    }
};
QTEST_MAIN(DeviceCycleUiTest)
#include "devicecycleui_test.moc"
