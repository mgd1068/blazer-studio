#include <QElapsedTimer>
#include <QJSValue>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <memory>

// UI contract double only. No Machine, serial backend or native input adapter.
class CommissioningModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap commissioning READ commissioning NOTIFY changed)
    Q_PROPERTY(bool requestPending MEMBER pending NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool firing MEMBER firing NOTIFY changed)
    Q_PROPERTY(bool connected MEMBER connected NOTIFY changed)
  public:
    bool pending = false, busy = false, firing = false, connected = true;
    int begins = 0, cancels = 0, stops = 0, attests = 0, homes = 0, probes = 0, corners = 0,
        zeros = 0, confirms = 0;
    QVariantList limitCalls;
    QHash<QString, QPointer<QObject>> nativeControls;
    QVariantMap data{{"stage", "inactive"},  {"fingerprint", QString()},
                     {"active", false},      {"homeObserved", false},
                     {"probesConfirmed", 0}, {"cornersConfirmed", 0},
                     {"cornerIndex", 0},     {"machineX", -10.},
                     {"machineY", -20.},     {"workX", 0.},
                     {"workY", 0.},          {"wcoX", -10.},
                     {"wcoY", -20.},         {"reason", "Controller-Messung ausstehend"}};
    QVariantMap commissioning() const { return data; }
    QString stage() const { return data.value("stage").toString(); }
    int motions() const { return homes + probes + corners + zeros; }
    void setStage(const QString &s, bool inFlight = false) {
        data["stage"] = s;
        data["active"] = s != "inactive" && s != "fault";
        pending = inFlight;
        busy = inFlight;
        emit changed();
    }
    void settled(const QString &s) { setStage(s); }
    Q_INVOKABLE void registerNativeActionControl(QObject *control, const QString &action) {
        nativeControls.insert(control->objectName(), control);
        control->setProperty("testRegisteredAction", action);
    }
    Q_INVOKABLE void beginCommissioning() {
        ++begins;
        setStage("readback");
    }
    Q_INVOKABLE void attestCommissioningMechanics(bool a, bool b, bool c, bool d) {
        if (stage() != "mechanics" || !(a && b && c && d))
            return;
        ++attests;
        setStage("homeReady");
    }
    Q_INVOKABLE void commissioningHome() {
        if (stage() != "homeReady")
            return;
        ++homes;
        setStage("homing", true);
    }
    Q_INVOKABLE void confirmCommissioningHome() {
        if (stage() != "geometry")
            return;
        data["homeObserved"] = true;
        emit changed();
    }
    Q_INVOKABLE void commissioningProbe(double dx, double dy) {
        if (stage() != "geometry" || !data["homeObserved"].toBool() ||
            std::abs(dx) + std::abs(dy) != 1)
            return;
        ++probes;
        setStage("probePending", true);
    }
    Q_INVOKABLE void confirmCommissioningProbe() {
        if (stage() != "probeConfirm")
            return;
        data["probesConfirmed"] = data["probesConfirmed"].toInt() + 1;
        setStage(data["probesConfirmed"].toInt() == 2 ? "limits" : "geometry");
    }
    Q_INVOKABLE void setCommissioningLimits(double x0, double y0, double x1, double y1) {
        limitCalls = {x0, y0, x1, y1};
        data["minX"] = x0;
        data["minY"] = y0;
        data["maxX"] = x1;
        data["maxY"] = y1;
        setStage("cornerReady");
    }
    Q_INVOKABLE void commissioningCorner(int index) {
        if (stage() != "cornerReady" || index != data["cornerIndex"].toInt())
            return;
        ++corners;
        setStage("cornerPending", true);
    }
    Q_INVOKABLE void confirmCommissioningCorner() {
        if (stage() != "cornerConfirm")
            return;
        const int n = data["cornersConfirmed"].toInt() + 1;
        data["cornersConfirmed"] = n;
        data["cornerIndex"] = std::min(n, 3);
        setStage(n == 4 ? "workCoordinates" : "cornerReady");
    }
    Q_INVOKABLE void commissioningZero() {
        if (stage() != "workCoordinates")
            return;
        ++zeros;
        setStage("zeroPending", true);
    }
    Q_INVOKABLE void confirmCommissioningCoordinates() {
        ++confirms;
        setStage("complete");
    }
    Q_INVOKABLE void cancelCommissioning() {
        ++cancels;
        setStage("inactive");
    }
    Q_INVOKABLE void stop() {
        ++stops;
        setStage("fault");
    }
  signals:
    void changed();
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
bool click(QQuickWindow *window, QQuickItem *item, bool enabled = true) {
    if (!item || (enabled && !item->isEnabled()) || !reveal(window, item))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {},
                      item->mapToScene(item->boundingRect().center()).toPoint());
    return true;
}
bool type(QQuickWindow *window, QQuickItem *item, const QString &text) {
    if (!click(window, item))
        return false;
    item->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (char c : text.toLatin1())
        QTest::keyClick(window, c);
    return true;
}
struct Harness {
    CommissioningModel model;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow *window = nullptr;
    QObject *dialog = nullptr;
    QString error;
    bool create(bool compact) {
        engine.rootContext()->setContextProperty("commissioningModel", &model);
        QString dir = qEnvironmentVariable("BLAZER_COMMISSIONING_QML_DIR");
        if (dir.isEmpty())
            dir = QStringLiteral(BLAZER_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        const auto qml = QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {
 width:%2; height:%3; visible:true
 property alias setup: setup
 App.M3CommissioningDialog { id:setup; modelMachine:commissioningModel; dark:%4 }
})")
                             .arg(QUrl::fromLocalFile(dir).toString())
                             .arg(compact ? 640 : 1200)
                             .arg(compact ? 520 : 900)
                             .arg(compact ? "true" : "false");
        component.setData(qml.toUtf8(), QUrl());
        root.reset(component.create());
        error = component.errorString();
        if (!root)
            return false;
        window = qobject_cast<QQuickWindow *>(root.get());
        dialog = root->property("setup").value<QObject *>();
        if (!window || !dialog)
            return false;
        if (!QTest::qWaitForWindowExposed(window, 1000))
            return false;
        return QMetaObject::invokeMethod(dialog, "open");
    }
    QQuickItem *item(const QString &name) { return find(window->contentItem(), name); }
    bool press(const QString &name, bool enabled = true) {
        return click(window, item(name), enabled);
    }
};
} // namespace
class CommissioningUiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QQuickStyle::setStyle("Basic"); }
    void workflow_data() {
        QTest::addColumn<bool>("compact");
        QTest::newRow("normal-light") << false;
        QTest::newRow("compact-dark") << true;
    }
    void workflow() {
        QFETCH(bool, compact);
        Harness h;
        QVERIFY2(h.create(compact), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        QCOMPARE(h.model.motions(), 0);
        h.model.setStage("mechanics");
        QTRY_VERIFY(h.item("attestSetupMechanics"));
        const QStringList checks = {"setupSwitchesWitness", "setupDirectionWitness",
                                    "setupFreeTravelWitness", "setupLaserDisabledWitness"};
        auto *attest = h.item("attestSetupMechanics");
        QVERIFY(!attest->isEnabled());
        for (int i = 0; i < checks.size(); ++i) {
            QVERIFY2(h.press(checks[i]), qPrintable(checks[i]));
            QCOMPARE(attest->isEnabled(), i == 3);
            QCOMPARE(h.model.motions(), 0);
        }
        // Each individual witness is necessary, even after all were checked.
        for (const auto &name : checks) {
            QVERIFY(h.press(name));
            QVERIFY(!attest->isEnabled());
            QVERIFY(h.press(name));
            QVERIFY(attest->isEnabled());
        }
        QVERIFY(h.press("attestSetupMechanics"));
        QCOMPARE(h.model.attests, 1);
        QCOMPARE(h.model.motions(), 0);
        QCOMPARE(h.model.nativeControls.size(), 7);
        auto *home = h.item("setupHomeButton");
        QVERIFY(home);
        QVERIFY(home->property("nativeActionEnabled").toBool());
        QVERIFY(h.press("setupHomeButton"));
        QCOMPARE(h.model.homes, 1);
        QCOMPARE(h.model.stage(), QString("homing"));
        QVERIFY(home->isVisible());
        QVERIFY(home->property("nativeActionEnabled").toBool());
        QVERIFY(!home->isEnabled());
        h.model.settled("geometry");
        QVERIFY(h.press("setupObservedHome"));
        QVERIFY(h.press("confirmSetupHome"));
        for (const QString name : {QString("setupProbe_1_0"), QString("setupProbe_0_1")}) {
            auto *probe = h.item(name);
            QVERIFY(probe);
            QVERIFY(probe->isEnabled());
            QVERIFY(h.press(name));
            QCOMPARE(h.model.stage(), QString("probePending"));
            QVERIFY(probe->isVisible());
            QVERIFY(probe->property("nativeActionEnabled").toBool());
            QVERIFY(!probe->isEnabled());
            h.model.settled("probeConfirm");
            QVERIFY(probe->property("nativeActionEnabled").toBool());
            QVERIFY(!probe->isEnabled());
            QVERIFY(!h.item("confirmSetupProbe")->isEnabled());
            QVERIFY(h.press("setupMeasuredProbe"));
            QVERIFY(h.press("confirmSetupProbe"));
        }
        QCOMPARE(h.model.stage(), QString("limits"));
        QCOMPARE(h.model.probes, 2);
        QVERIFY(type(h.window, h.item("setupMinX"), "-190,5"));
        QVERIFY(type(h.window, h.item("setupMaxX"), "-0.5"));
        QVERIFY(type(h.window, h.item("setupMinY"), "-140"));
        QVERIFY(type(h.window, h.item("setupMaxY"), "-1"));
        QVERIFY(h.press("submitSetupLimits"));
        QCOMPARE(h.model.limitCalls, QVariantList({-190.5, -140., -.5, -1.}));
        for (int i = 0; i < 4; ++i) {
            auto *corner = h.item("setupCornerButton");
            QVERIFY(corner->property("nativeActionEnabled").toBool());
            const auto args =
                corner->property("nativeActionArguments").value<QJSValue>().toVariant().toMap();
            QCOMPARE(args.value("index").toInt(), i);
            QVERIFY(h.press("setupCornerButton"));
            QCOMPARE(h.model.corners, i + 1);
            QVERIFY(corner->isVisible());
            QVERIFY(corner->property("nativeActionEnabled").toBool());
            QVERIFY(!corner->isEnabled());
            h.model.settled("cornerConfirm");
            QVERIFY(corner->property("nativeActionEnabled").toBool());
            QVERIFY(!h.item("confirmSetupCorner")->isEnabled());
            QVERIFY(h.press("setupObservedCorner"));
            QVERIFY(h.press("confirmSetupCorner"));
        }
        QCOMPARE(h.model.stage(), QString("workCoordinates"));
        auto *zero = h.item("setupZeroButton");
        QVERIFY(h.press("setupCoordinatesWitness"));
        QVERIFY(h.press("setupZeroButton"));
        QCOMPARE(h.model.zeros, 1);
        QVERIFY(zero->isVisible());
        QVERIFY(zero->property("nativeActionEnabled").toBool());
        QVERIFY(!zero->isEnabled());
        QVERIFY(!h.item("setupCoordinatesWitness")->property("checked").toBool());
        h.model.settled("workCoordinates");
        QVERIFY(h.press("setupCoordinatesWitness"));
        QVERIFY(h.press("confirmSetupCoordinates"));
        QCOMPARE(h.model.confirms, 1);
        QCOMPARE(h.model.stage(), QString("complete"));
        QVERIFY(h.press("closeSetup"));
        QTRY_VERIFY(!h.dialog->property("visible").toBool());
        QCOMPARE(h.model.cancels, 0);
        QCOMPARE(h.model.motions(), 8); // one home, two probes, four corners, one explicit zero
        for (auto it = h.model.nativeControls.cbegin(); it != h.model.nativeControls.cend(); ++it)
            QVERIFY(it.value());
    }
    void invalidLimits_data() {
        QTest::addColumn<QString>("value");
        for (const auto &v : {"", "-", "+", ".", "-.", "NaN", "Infinity", "1e3", "2 mm", "1.2.3",
                              "999999999999999999"})
            QTest::newRow(v) << QString::fromLatin1(v);
    }
    void invalidLimits() {
        QFETCH(QString, value);
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        h.model.setStage("limits");
        for (const auto &name : {"setupMinX", "setupMinY"})
            QVERIFY(type(h.window, h.item(name), "-100"));
        for (const auto &name : {"setupMaxX", "setupMaxY"})
            QVERIFY(type(h.window, h.item(name), "-1"));
        QVERIFY(h.item("submitSetupLimits")->isEnabled());
        QVERIFY(type(h.window, h.item("setupMinX"), value));
        QVERIFY2(!h.item("submitSetupLimits")->isEnabled(),
                 qPrintable("invalid min accepted: " + value));
        QVERIFY(h.press("submitSetupLimits", false));
        QVERIFY(h.model.limitCalls.isEmpty());
        QCOMPARE(h.model.motions(), 0);
    }
    void cancelEveryStage_data() {
        QTest::addColumn<QString>("stage");
        for (const auto &s :
             {"readback", "mechanics", "homeReady", "homing", "geometry", "probePending",
              "probeConfirm", "limits", "cornerReady", "cornerPending", "cornerConfirm",
              "workCoordinates", "zeroPending", "fault"})
            QTest::newRow(s) << QString::fromLatin1(s);
    }
    void cancelEveryStage() {
        QFETCH(QString, stage);
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        h.model.setStage(stage, stage.endsWith("Pending") || stage == "homing");
        QVERIFY(h.press("closeSetup"));
        QTRY_COMPARE(h.model.cancels, 1);
        QCOMPARE(h.model.motions(), 0);
    }
    void stopEveryStage_data() { cancelEveryStage_data(); }
    void stopEveryStage() {
        QFETCH(QString, stage);
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        h.model.setStage(stage, true);
        QVERIFY(h.press("stopSetup"));
        QCOMPARE(h.model.stops, 1);
        QCOMPARE(h.model.motions(), 0);
    }
    void unknownPositionsAndPlainReason() {
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        for (const auto &key : {"machineX", "machineY", "workX", "workY", "wcoX", "wcoY"})
            h.model.data[key] = std::numeric_limits<double>::quiet_NaN();
        h.model.data["reason"] = "<b>Nur Controllertext</b>";
        h.model.setStage("readback");
        QCOMPARE(h.item("commissioningPosition")->property("text").toString().count("unbekannt"),
                 6);
        QCOMPARE(h.item("commissioningReason")->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(h.item("commissioningReason")->property("text").toString(),
                 QString("<b>Nur Controllertext</b>"));
        QVERIFY(h.press("closeSetup"));
        QTRY_COMPARE(h.model.cancels, 1);
        QCOMPARE(h.model.motions(), 0);
    }
    void zeroPendingCannotAttestCoordinatesWithLaggingBusyFlags() {
        Harness h;
        QVERIFY2(h.create(false), qPrintable(h.error));
        QTRY_COMPARE(h.model.begins, 1);
        h.model.setStage("workCoordinates");
        QVERIFY(h.press("setupCoordinatesWitness"));
        h.model.setStage("zeroPending",
                         false); // Stage delivered before the independent pending flag.
        QVERIFY(!h.item("confirmSetupCoordinates")->isEnabled());
        QVERIFY(h.press("confirmSetupCoordinates", false));
        QCOMPARE(h.model.confirms, 0);
    }
};
QTEST_MAIN(CommissioningUiTest)
#include "commissioningui_test.moc"
