#include <QDir>
#include <QElapsedTimer>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <algorithm>
#include <memory>

// Exact Machine UI contract double; snapshots advance only explicitly in tests.
// No Machine/Core/serial or physical/native-input adapter is linked.
class ManualStartActor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap operatorLocalFrame READ operatorLocalFrame NOTIFY changed)
    Q_PROPERTY(bool requestPending MEMBER pending NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool firing MEMBER firing NOTIFY changed)
    Q_PROPERTY(bool paused MEMBER paused NOTIFY changed)
    Q_PROPERTY(bool connected MEMBER connected NOTIFY changed)
    Q_PROPERTY(bool statusFresh MEMBER fresh NOTIFY changed)
    Q_PROPERTY(bool holdConfirmed MEMBER hold NOTIFY changed)
    Q_PROPERTY(bool jobPreparing MEMBER controllerPreparing NOTIFY changed)
    Q_PROPERTY(QVariantMap extractionSupervision READ extractionSupervision NOTIFY changed)
  public:
    struct Invocation {
        QString action;
        QVariantList arguments;
    };
    struct NativeControl {
        QPointer<QObject> control;
        QString action;
        QVariantMap arguments;
    };
    bool pending = false, busy = false, firing = false, paused = false, connected = true,
         fresh = true;
    bool hold = false, controllerPreparing = false;
    QVariantMap extraction{{"mode", "sensorVerified"},
                           {"notice", "Luftstrom nicht gemessen"},
                           {"reason", ""},
                           {"activeLease", false},
                           {"pendingAction", ""},
                           {"remainingMs", 0}};
    QVariantMap extractionSupervision() const { return extraction; }
    Q_INVOKABLE void selectAttendedExtraction() {
        record("selectAttendedExtraction");
        extraction["mode"] = "operatorAttendedExternal";
        emit changed();
    }
    Q_INVOKABLE void attestExternalExtraction(const QString &action, bool running, bool present) {
        record("attestExternalExtraction", {action, running, present});
    }
    Q_INVOKABLE void revokeExternalAttendance() { record("revokeExternalAttendance"); }
    Q_INVOKABLE void confirmExtractionClearance(bool confirmed) {
        record("confirmExtractionClearance", {confirmed});
    }
    Q_INVOKABLE void pause() { record("pause"); }
    int outputs() const {
        return count("startLocalJob") + count("frameLocalJob") + count("resumeLocalJob") +
               count("start") + count("frame") + count("resume");
    }
    QVariantMap snapshot{{"phase", "inactive"},
                         {"reason", "Restwege unbekannt"},
                         {"frameId", ""},
                         {"approvalId", ""},
                         {"sourceFingerprint", ""},
                         {"coordinateEpoch", 0},
                         {"localPositionKnown", false},
                         {"marginMm", 2.},
                         {"available", QVariantMap{}},
                         {"usableAfterMargin", QVariantMap{}},
                         {"requiredActualWire", QVariantMap{}},
                         {"fits", false},
                         {"prepared", false},
                         {"approved", false},
                         {"startReady", false},
                         {"frameReady", false},
                         {"frameReason", ""},
                         {"resumeReady", false},
                         {"motorIdleDelayMs", 0},
                         {"doorSubstate", "unknown"}};
    QVariantMap operatorLocalFrame() const { return snapshot; }
    Q_INVOKABLE void beginManualPlacement() { record("beginManualPlacement"); }
    Q_INVOKABLE void setManualTravel(double negX, double posX, double negY, double posY) {
        record("setManualTravel", {negX, posX, negY, posY});
    }
    Q_INVOKABLE void attestManualPlacement(bool motorsFree, bool measured, bool stationary,
                                           bool lidClosed) {
        record("attestManualPlacement", {motorsFree, measured, stationary, lidClosed});
    }
    Q_INVOKABLE void captureOperatorLocalFrame() { record("captureOperatorLocalFrame"); }
    Q_INVOKABLE void prepareLocalJob() { record("prepareLocalJob"); }
    Q_INVOKABLE void approveLocalTravel(const QString &approvalId) {
        record("approveLocalTravel", {approvalId});
    }
    Q_INVOKABLE void startLocalJob() { record("startLocalJob"); }
    Q_INVOKABLE void frameLocalJob() { record("frameLocalJob"); }
    Q_INVOKABLE void resumeLocalJob() { record("resumeLocalJob"); }
    Q_INVOKABLE void cancelOperatorLocalFrame() { record("cancelOperatorLocalFrame"); }
    QList<Invocation> invocations;
    QList<NativeControl> nativeControls;
    void publish(const QVariantMap &patch) {
        for (auto it = patch.cbegin(); it != patch.cend(); ++it)
            snapshot[it.key()] = it.value();
        emit changed();
    }
    void record(const QString &action, const QVariantList &arguments = {}) {
        invocations.append({action, arguments}); // Deliberately no automatic phase progression.
    }
    int count(const QString &action) const {
        return std::count_if(invocations.cbegin(), invocations.cend(),
                             [&](const auto &call) { return call.action == action; });
    }
    Q_INVOKABLE void registerNativeActionControl(QObject *control, const QString &action,
                                                 const QVariantMap &fixedArguments = {}) {
        nativeControls.append({control, action, fixedArguments});
    }
    Q_INVOKABLE void stop() { record("stop"); }
    Q_INVOKABLE void start() { record("start"); }
    Q_INVOKABLE void resume() { record("resume"); }
    Q_INVOKABLE void frame(const QVariantMap &bounds) { record("frame", {bounds}); }
  signals:
    void changed();
};
class ManualStartProject : public QObject {
    Q_OBJECT
    Q_PROPERTY(double bedWidth MEMBER width NOTIFY changed)
    Q_PROPERTY(double bedHeight MEMBER height NOTIFY changed)
    Q_PROPERTY(bool planningBusy MEMBER planning NOTIFY changed)
    Q_PROPERTY(int jobOrigin MEMBER origin NOTIFY changed)
    Q_PROPERTY(int startMode MEMBER mode NOTIFY changed)
  public:
    double width = 200, height = 150;
    bool planning = false;
    int origin = 0, mode = 0;
    QList<int> anchors, modes;
    Q_INVOKABLE bool setJobOrigin(int value) {
        if (value < 0 || value > 8)
            return false;
        origin = value;
        anchors.append(value);
        emit changed();
        return true;
    }
    Q_INVOKABLE bool setStartMode(int value) {
        if (value < 0 || value > 2)
            return false;
        mode = value;
        modes.append(value);
        emit changed();
        return true;
    }
    void update() { emit changed(); }
  signals:
    void changed();
};
namespace {
const QStringList travelFields = {"manualNegativeX", "manualPositiveX", "manualNegativeY",
                                  "manualPositiveY"};
const QStringList witnesses = {"manualMotorsFreeWitness", "manualMeasuredTravelWitness",
                               "manualHeadStationaryWitness", "manualCoverClosedWitness"};
QVariantMap travel() {
    return {{"negativeX", 37.}, {"positiveX", 153.}, {"negativeY", 49.}, {"positiveY", 91.}};
}
QVariantMap usable() {
    return {{"negativeX", 35.}, {"positiveX", 151.}, {"negativeY", 47.}, {"positiveY", 89.}};
}
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
struct ManualStartHarness {
    ManualStartProject project;
    ManualStartActor actor;
    QQmlEngine engine;
    std::unique_ptr<QObject> root, dialog;
    QQuickWindow *window = nullptr;
    QString error;
    bool create(bool compact, QVariantMap properties = {}) {
        QString dir = qEnvironmentVariable("BLAZER_MANUALSTART_QML_DIR");
        if (dir.isEmpty())
            dir = QStringLiteral(BLAZER_SOURCE_DIR "/qml");
        const auto componentUrl = QUrl::fromLocalFile(dir + "/ManualStartDialog.qml");
        engine.rootContext()->setContextProperty("manualStartActor", &actor);
        QQmlComponent host(&engine);
        host.setData(QString("import QtQuick; import QtQuick.Controls; ApplicationWindow "
                             "{width:%1; height:%2; visible:true}")
                         .arg(compact ? 640 : 1200)
                         .arg(compact ? 520 : 900)
                         .toUtf8(),
                     QUrl());
        root.reset(host.create());
        error = host.errorString();
        if (!root)
            return false;
        window = qobject_cast<QQuickWindow *>(root.get());
        if (!window || !QTest::qWaitForWindowExposed(window, 1000))
            return false;
        QQmlComponent component(&engine, componentUrl);
        properties["parent"] = QVariant::fromValue(window->contentItem());
        properties["modelMachine"] = QVariant::fromValue<QObject *>(&actor);
        properties["dark"] = compact;
        properties["modelProject"] = QVariant::fromValue<QObject *>(&project);
        dialog.reset(component.createWithInitialProperties(properties));
        error = component.errorString();
        return dialog && QMetaObject::invokeMethod(dialog.get(), "open");
    }
    void phase(const QString &value, bool pending = false) {
        actor.pending = pending;
        actor.publish({{"phase", value}});
    }
    bool fillTravel() {
        const QStringList values = {"37", "153", "49", "91"};
        for (int n = 0; n < 4; ++n)
            if (!type(travelFields[n], values[n]))
                return false;
        return true;
    }
    void captured() {
        actor.publish({{"phase", "travelReview"},
                       {"localPositionKnown", true},
                       {"localX", 0.},
                       {"localY", 0.},
                       {"frameId", "frame-A"},
                       {"sourceFingerprint", "source-A"},
                       {"available", travel()},
                       {"usableAfterMargin", usable()},
                       {"requiredActualWire", usable()},
                       {"fits", true},
                       {"prepared", true},
                       {"frameReady", true},
                       {"approved", false},
                       {"approvalId", "approval-A"},
                       {"startReady", false}});
    }
    QQuickItem *item(const QString &name) { return find(window->contentItem(), name); }
    bool press(const QString &name, bool enabled = true) {
        return click(window, item(name), enabled);
    }
    bool type(const QString &name, const QString &text) {
        auto *target = item(name);
        if (!click(window, target))
            return false;
        target->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Backspace);
        for (char c : text.toLatin1())
            QTest::keyClick(window, c);
        return true;
    }
};
} // namespace
class ManualStartUiTest : public QObject {
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
        ManualStartHarness h;
        QVERIFY2(h.create(compact), qPrintable(h.error));
        QCOMPARE(h.actor.invocations.size(), 0);
        QCOMPARE(h.project.mode, 0);
        QCOMPARE(h.actor.nativeControls.size(), 4);
        QMap<QString, QString> registered;
        for (const auto &entry : h.actor.nativeControls) {
            QVERIFY(entry.control);
            registered[entry.control->objectName()] = entry.action;
        }
        const QMap<QString, QString> expectedActions{{"captureOperatorLocalFrame", "localCapture"},
                                                     {"startLocalJob", "localStart"},
                                                     {"frameLocalJob", "localFrame"},
                                                     {"resumeLocalJob", "localResume"}};
        QCOMPARE(registered, expectedActions);
        QVERIFY(h.item("manualLocalPosition")
                    ->property("text")
                    .toString()
                    .contains("noch nicht bestätigt"));
        QVERIFY(h.press("beginManualPlacement"));
        QCOMPARE(h.actor.count("beginManualPlacement"), 1);
        QCOMPARE(h.project.mode, 1);
        QVERIFY(!h.item("beginManualPlacement")->isEnabled());
        QCOMPARE(h.actor.outputs(), 0);
        h.phase("reading", true);
        QCOMPARE(h.actor.outputs(), 0);
        h.phase("positioning");
        for (const auto &name : travelFields)
            QVERIFY(h.item(name)->property("text").toString().isEmpty());
        QVERIFY(!h.dialog->property("draftComplete").toBool());
        QVERIFY(!h.item("setManualTravel")->isEnabled());
        QVERIFY(h.fillTravel());
        QVERIFY(h.press("setManualTravel"));
        QCOMPARE(h.actor.invocations.last().arguments, QVariantList({37., 153., 49., 91.}));
        QVERIFY(!h.item("attestManualPlacement")->isEnabled());
        h.actor.publish({{"available", travel()}});
        for (int n = 0; n < 4; ++n) {
            QVERIFY(h.press(witnesses[n]));
            QCOMPARE(h.item("attestManualPlacement")->isEnabled(), n == 3);
        }
        for (const auto &name : witnesses) {
            QVERIFY(h.press(name));
            QVERIFY(!h.item("attestManualPlacement")->isEnabled());
            QVERIFY(h.press(name));
        }
        QVERIFY(h.press("attestManualPlacement"));
        QCOMPARE(h.actor.invocations.last().arguments, QVariantList({true, true, true, true}));
        QCOMPARE(h.actor.outputs(), 0);
        h.phase("captureReady");
        QVERIFY(h.press("captureOperatorLocalFrame"));
        QCOMPARE(h.actor.count("captureOperatorLocalFrame"), 1);
        QCOMPARE(h.actor.outputs(), 0);
        auto *capture = h.item("captureOperatorLocalFrame");
        for (const auto &stage : {"resetting", "capturing"}) {
            h.phase(stage, true);
            QVERIFY(capture->isVisible());
            QVERIFY(capture->property("nativeActionEnabled").toBool());
            QVERIFY(!capture->isEnabled());
            QCOMPARE(h.actor.outputs(), 0);
        }
        h.actor.pending = false;
        h.actor.controllerPreparing = true;
        h.actor.publish(
            {{"phase", "preparing"}, {"localPositionKnown", true}, {"localX", 0.}, {"localY", 0.}});
        QVERIFY(!h.item("prepareLocalJob")->isEnabled());
        QCOMPARE(h.actor.outputs(), 0);
        h.actor.controllerPreparing = false;
        h.actor.publish({});
        h.project.planning = true;
        h.project.update();
        QVERIFY(!h.item("prepareLocalJob")->isEnabled());
        h.project.planning = false;
        h.project.update();
        QCOMPARE(h.item("manualRequired_negativeX")->property("text").toString(),
                 QString("unbekannt"));
        QVERIFY(h.press("prepareLocalJob"));
        QCOMPARE(h.actor.count("prepareLocalJob"), 1);
        h.captured();
        for (int anchor = 0; anchor < 9; ++anchor) {
            QVERIFY(h.press("manualAnchor" + QString::number(anchor)));
            QCOMPARE(h.project.origin, anchor);
        }
        QCOMPARE(h.project.anchors, QList<int>({0, 1, 2, 3, 4, 5, 6, 7, 8}));
        for (const auto &key : {"negativeX", "positiveX", "negativeY", "positiveY"}) {
            QCOMPARE(h.item("manualRequired_" + QString(key))->property("text").toString(),
                     QString::number(usable()[key].toDouble(), 'f', 3) + " mm");
            QCOMPARE(h.item("manualAvailable_" + QString(key))->property("text").toString(),
                     QString::number(usable()[key].toDouble(), 'f', 3) + " mm");
        }
        auto excess = usable();
        excess["negativeX"] = 35.001;
        h.actor.publish({{"fits", false},
                         {"requiredActualWire", excess},
                         {"reason", "−X benötigt 35.001 mm, nutzbar 35.000 mm"}});
        QVERIFY(!h.item("approveLocalTravel")->isEnabled());
        QVERIFY(!h.item("frameLocalJob")->isEnabled());
        QCOMPARE(h.item("manualRequired_negativeX")->property("text").toString(),
                 QString("35.001 mm"));
        QVERIFY(h.item("manualTravelVerdict")->property("text").toString().contains("35.001"));
        const auto evidence = qEnvironmentVariable("BLAZER_MANUALSTART_UI_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(reveal(h.window, h.item("manualTravelVerdict")));
            QVERIFY(h.window->grabWindow().save(
                evidence +
                (compact ? "/manual-start-compact-dark.png" : "/manual-start-normal-light.png")));
        }

        h.actor.publish(
            {{"fits", true}, {"requiredActualWire", usable()}, {"approvalId", "approval-B"}});
        QVERIFY(h.press("approveLocalTravel"));
        QCOMPARE(h.actor.invocations.last().arguments, QVariantList({"approval-B"}));
        QCOMPARE(h.actor.outputs(), 0);
        h.actor.publish({{"approved", true}, {"phase", "ready"}});
        QVERIFY(!h.item("startLocalJob")->isEnabled());
        QCOMPARE(h.actor.outputs(), 0);
        QVERIFY(h.item("selectAttendedExtraction")
                    ->isEnabled()); // Local setup is enough; no global homing grant.
        QVERIFY(h.press("selectAttendedExtraction"));
        QCOMPARE(h.actor.count("selectAttendedExtraction"), 1);
        QCOMPARE(h.actor.outputs(), 0);
        h.actor.publish({{"startReady", true}});
        QCOMPARE(h.actor.outputs(), 0);
        QVERIFY(h.press("frameLocalJob"));
        QCOMPARE(h.actor.count("frameLocalJob"), 1);
        h.phase("ready", true);
        QVERIFY(h.item("frameLocalJob")->property("nativeActionEnabled").toBool());
        QVERIFY(!h.item("frameLocalJob")->isEnabled());
        h.phase("ready");
        QVERIFY(h.press("startLocalJob"));
        QCOMPARE(h.actor.count("startLocalJob"), 1);
        h.actor.busy = true;
        h.actor.firing = true;
        h.phase("running");
        QVERIFY(!h.item("startLocalJob")->isEnabled());
        QVERIFY(h.item("startLocalJob")->property("nativeActionEnabled").toBool());
        QVERIFY(h.press("pauseLocalJob"));
        QCOMPARE(h.actor.count("pause"), 1);
        h.actor.firing = false;
        h.actor.paused = true;
        h.actor.hold = true;
        h.phase("paused");
        QVERIFY(!h.item("resumeLocalJob")->isEnabled());
        h.actor.publish({{"resumeReady", true}});
        QVERIFY(h.press("resumeLocalJob"));
        QCOMPARE(h.actor.count("resumeLocalJob"), 1);
        h.phase("paused", true);
        QVERIFY(!h.item("resumeLocalJob")->isEnabled());
        QVERIFY(h.item("resumeLocalJob")->property("nativeActionEnabled").toBool());
        QVERIFY(h.press("stopManualStart"));
        QCOMPARE(h.actor.count("stop"), 1);
    }
    void frameNeedsSeparatePreparedFootprint() {
        ManualStartHarness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.captured();
        h.actor.publish({{"phase", "ready"},
                         {"approved", true},
                         {"startReady", true},
                         {"frameReady", false},
                         {"frameReason", "Separater Rahmenpfad mit Rückfahrt überschreitet +X."}});
        QVERIFY(h.item("startLocalJob")->isEnabled());
        QVERIFY(!h.item("frameLocalJob")->isEnabled());
        QVERIFY(h.press("frameLocalJob", false));
        QCOMPARE(h.actor.count("frameLocalJob"), 0);
        QCOMPARE(h.item("manualFrameReason")->property("text").toString(),
                 QString("Separater Rahmenpfad mit Rückfahrt überschreitet +X."));
        h.actor.publish({{"frameReady", true}, {"frameReason", ""}});
        QVERIFY(h.press("frameLocalJob"));
        QCOMPARE(h.actor.count("frameLocalJob"), 1);
        QCOMPARE(h.actor.count("startLocalJob"), 0);
    }
    void invalidTravel_data() {
        QTest::addColumn<QString>("value");
        for (const auto &v : {"", "-", ".", "NaN", "Infinity", "1e3", "-1", "201", "0", "1.999"})
            QTest::newRow(v) << QString::fromLatin1(v);
    }
    void invalidTravel() {
        QFETCH(QString, value);
        ManualStartHarness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.phase("positioning");
        QVERIFY(h.fillTravel());
        QVERIFY(h.type("manualNegativeX", value));
        QVERIFY(!h.item("setManualTravel")->isEnabled());
        QVERIFY(h.press("setManualTravel", false));
        QCOMPARE(h.actor.count("setManualTravel"), 0);
        QCOMPARE(h.actor.outputs(), 0);
        if (value == "0") {
            QVERIFY(h.dialog->property("draftComplete").toBool());
            QVERIFY(h.item("manualDraftError")->property("text").toString().contains("mindestens"));
        }
        if (value.isEmpty())
            QVERIFY(!h.dialog->property("draftComplete").toBool());
    }
    void editingTravelRevokesFourDraftWitnesses() {
        ManualStartHarness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.phase("positioning");
        QVERIFY(h.fillTravel());
        h.actor.publish({{"available", travel()}});
        for (const auto &name : witnesses)
            QVERIFY(h.press(name));
        QVERIFY(h.item("attestManualPlacement")->isEnabled());
        QVERIFY(h.type("manualNegativeX", "38"));
        for (const auto &name : witnesses)
            QVERIFY(!h.item(name)->property("checked").toBool());
        QVERIFY(!h.item("attestManualPlacement")->isEnabled());
        QCOMPARE(h.actor.outputs(), 0);
    }
    void stopAllPhases_data() {
        QTest::addColumn<QString>("phase");
        for (const auto &s :
             {"inactive", "reading", "positioning", "captureReady", "resetting", "capturing",
              "preparing", "travelReview", "ready", "running", "paused", "invalid"})
            QTest::newRow(s) << QString::fromLatin1(s);
    }
    void stopAllPhases() {
        QFETCH(QString, phase);
        ManualStartHarness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.actor.busy = true;
        h.actor.firing = true;
        h.phase(phase, true);
        QVERIFY(h.press("stopManualStart"));
        QCOMPARE(h.actor.count("stop"), 1);
        QCOMPARE(h.actor.outputs(), 0);
    }
    void invalidationSnapshotsBlockOutput_data() {
        QTest::addColumn<QString>("reason");
        for (const auto &s : {"Jobquelle geändert", "Tür offen", "Controller Reset"})
            QTest::newRow(s) << QString::fromUtf8(s);
    }
    void invalidationSnapshotsBlockOutput() {
        QFETCH(QString, reason);
        ManualStartHarness h;
        QVERIFY2(h.create(false), qPrintable(h.error));
        h.captured();
        h.actor.publish({{"approved", true}, {"startReady", true}, {"phase", "ready"}});
        QVERIFY(h.item("startLocalJob")->isEnabled());
        h.actor.publish({{"phase", "invalid"},
                         {"reason", reason},
                         {"localPositionKnown", false},
                         {"prepared", false},
                         {"fits", false},
                         {"approved", false},
                         {"startReady", false},
                         {"approvalId", ""},
                         {"sourceFingerprint", "source-B"}});
        QVERIFY(!h.item("startLocalJob")->isVisible());
        QVERIFY(!h.item("startLocalJob")->property("nativeActionEnabled").toBool());
        QCOMPARE(h.item("manualStartReason")->property("text").toString(), reason);
        QCOMPARE(h.actor.outputs(), 0);
    }
    void closePreservesCapturedFrameButCancelsUncapturedSetup() {
        {
            ManualStartHarness h;
            QVERIFY2(h.create(false), qPrintable(h.error));
            h.captured();
            QVERIFY(h.press("closeManualStart"));
            QTRY_VERIFY(!h.dialog->property("visible").toBool());
            QCOMPARE(h.actor.count("cancelOperatorLocalFrame"), 0);
        }
        {
            ManualStartHarness h;
            QVERIFY2(h.create(false), qPrintable(h.error));
            h.phase("positioning");
            QVERIFY(h.press("closeManualStart"));
            QTRY_COMPARE(h.actor.count("cancelOperatorLocalFrame"), 1);
        }
    }
    void closeDuringNewBeginCancelsIntentDespiteOldCapturedSnapshot() {
        ManualStartHarness h;
        QVERIFY2(h.create(false), qPrintable(h.error));
        h.captured();
        QVERIFY(h.press("beginManualPlacement"));
        QVERIFY(h.dialog->property("beginPending").toBool());
        QVERIFY(h.press("closeManualStart"));
        QTRY_COMPARE(h.actor.count("cancelOperatorLocalFrame"), 1);
    }
};
QTEST_MAIN(ManualStartUiTest)
#include "manualstartui_test.moc"
