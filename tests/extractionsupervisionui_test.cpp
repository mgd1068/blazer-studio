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

// UI API double only: no Machine, controller, serial or native input adapter.
class ExtractionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap extractionSupervision READ extractionSupervision NOTIFY changed)
    Q_PROPERTY(bool requestPending MEMBER pending NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool firing MEMBER firing NOTIFY changed)
    Q_PROPERTY(bool paused MEMBER paused NOTIFY changed)
  public:
    bool pending = false, busy = false, firing = false, paused = false;
    int selections = 0, clearances = 0, attests = 0, revocations = 0, outputStarts = 0;
    QStringList actions;
    QVariantMap state{{"mode", "sensorVerified"},
                      {"notice", "Luftstrom ist nicht gemessen."},
                      {"reason", ""},
                      {"pendingAction", ""},
                      {"remainingMs", 0},
                      {"activeLease", false}};
    QVariantMap extractionSupervision() const { return state; }
    void update() { emit changed(); }
    Q_INVOKABLE void confirmExtractionClearance(bool checked) {
        if (checked)
            ++clearances;
    }
    Q_INVOKABLE void selectAttendedExtraction() {
        ++selections;
        state["mode"] = "operatorAttendedExternal";
        emit changed();
    }
    Q_INVOKABLE void attestExternalExtraction(const QString &action, bool running, bool present) {
        if (!running || !present)
            return;
        ++attests;
        actions.append(action);
        state["pendingAction"] = action;
        state["remainingMs"] = 30000;
        emit changed();
    }
    Q_INVOKABLE void revokeExternalAttendance() {
        ++revocations;
        state["pendingAction"] = "";
        state["activeLease"] = false;
        emit changed();
    }
    // Deliberately forbidden in this panel: attestation must not call output actions.
    Q_INVOKABLE void start() { ++outputStarts; }
    Q_INVOKABLE void focus() { ++outputStarts; }
    Q_INVOKABLE void resume() { ++outputStarts; }
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
bool click(QQuickWindow *window, QQuickItem *item, bool requireEnabled = true) {
    if (!item || (requireEnabled && !item->isEnabled()) || !reveal(window, item))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {},
                      item->mapToScene(item->boundingRect().center()).toPoint());
    return true;
}
struct Harness {
    ExtractionModel model;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow *window = nullptr;
    QQuickItem *panel = nullptr;
    QString error;
    bool create(bool compact) {
        engine.rootContext()->setContextProperty("extractionModel", &model);
        QString dir = qEnvironmentVariable("BLAZER_EXTRACTION_QML_DIR");
        if (dir.isEmpty())
            dir = QStringLiteral(BLAZER_SOURCE_DIR "/qml");
        QQmlComponent component(&engine);
        component.setData(QString(R"(import QtQuick
import QtQuick.Controls
import "%1" as App
ApplicationWindow {
 width:%2; height:%3; visible:true
 ScrollView {
  anchors.fill:parent; anchors.margins:12; clip:true; contentWidth:availableWidth
  App.ExtractionSupervisionPanel { width:parent.width; modelMachine:extractionModel; dark:%4 }
 }
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
    bool selectAction(int index) {
        auto *combo = item("extractionActionChoice");
        if (!click(window, combo))
            return false;
        QTest::keyClick(window, Qt::Key_Home);
        for (int n = 0; n < index; ++n)
            QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        return combo->property("currentIndex").toInt() == index;
    }
};
const QStringList witnesses = {"externalExtractionOnWitness", "operatorPresentWitness",
                               "attendedWorkAreaWitness"};
} // namespace
class ExtractionSupervisionUiTest : public QObject {
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
        QCOMPARE(h.model.selections, 0);
        QCOMPARE(h.model.clearances, 0);
        QCOMPARE(h.model.attests, 0);
        QCOMPARE(h.model.outputStarts, 0);
        QVERIFY(!h.item("selectAttendedExtraction")->isEnabled());
        h.panel->setProperty("commissioningComplete", true);
        QVERIFY(h.item("selectAttendedExtraction")->isEnabled());
        QVERIFY(!h.item("confirmExtractionClearance")->isEnabled());
        QVERIFY(h.press("extractionClearanceWitness"));
        QVERIFY(h.press("confirmExtractionClearance"));
        QCOMPARE(h.model.clearances, 1);
        QVERIFY(!h.item("extractionClearanceWitness")->property("checked").toBool());
        QVERIFY(!h.item("confirmExtractionClearance")->isEnabled());
        QCOMPARE(h.model.selections, 0);
        QVERIFY(h.press("selectAttendedExtraction"));
        QCOMPARE(h.model.selections, 1);
        QCOMPARE(h.model.attests, 0);
        QVERIFY(!h.item("selectAttendedExtraction")->isVisible());
        const QStringList actions = {"start", "focus", "resume"};
        for (int index = 0; index < actions.size(); ++index) {
            h.model.paused = index == 2;
            h.model.busy = index == 2;
            h.model.update();
            QVERIFY(h.selectAction(index));
            auto *attest = h.item("attestExternalExtraction");
            QVERIFY(!attest->isEnabled());
            for (int i = 0; i < witnesses.size(); ++i) {
                QVERIFY(h.press(witnesses[i]));
                QCOMPARE(attest->isEnabled(), i == 2);
            }
            // All three observations independently gate attestation.
            for (const auto &name : witnesses) {
                QVERIFY(h.press(name));
                QVERIFY(!attest->isEnabled());
                QVERIFY(h.press(name));
            }
            QVERIFY(h.press("attestExternalExtraction"));
            QCOMPARE(h.model.attests, index + 1);
            QCOMPARE(h.model.actions.last(), actions[index]);
            for (const auto &name : witnesses)
                QVERIFY(!h.item(name)->property("checked").toBool());
            QVERIFY(!attest->isEnabled());
            QCOMPARE(h.model.outputStarts, 0);
            QVERIFY(h.item("extractionWitnessStatus")
                        ->property("text")
                        .toString()
                        .contains("Starttaste"));
            // Receipt of a pending witness and an elapsed deadline cannot start output.
            h.model.state["remainingMs"] = 0;
            h.model.update();
            QTest::qWait(10);
            QCOMPARE(h.model.outputStarts, 0);
        }
        h.model.state["activeLease"] = true;
        h.model.firing = true;
        h.model.busy = true;
        h.model.update();
        QVERIFY(!h.item("confirmExtractionClearance")->isEnabled());
        QVERIFY(!h.item("extractionClearanceWitness")->isEnabled());
        QVERIFY(h.press("revokeExternalAttendance"));
        QCOMPARE(h.model.revocations, 1);
        QCOMPARE(h.model.outputStarts, 0);
        for (const auto &name : witnesses)
            QVERIFY(!h.item(name)->property("checked").toBool());
    }
    void focusChoiceSurvivesUnrelatedModelChanges_data() { workflow_data(); }
    void focusChoiceSurvivesUnrelatedModelChanges() {
        QFETCH(bool, compact);
        Harness h;
        QVERIFY2(h.create(compact), qPrintable(h.error));
        h.panel->setProperty("commissioningComplete", true);
        QVERIFY(h.press("selectAttendedExtraction"));
        QVERIFY(!h.model.paused);
        QVERIFY(h.selectAction(1)); // Actual ComboBox mouse/keyboard choice, not setProperty.
        auto *choice = h.item("extractionActionChoice");
        QCOMPARE(choice->property("currentIndex").toInt(), 1);
        for (int remaining : {29000, 17000, 0}) {
            h.model.state["remainingMs"] = remaining;
            h.model.state["reason"] = QString("Status aktualisiert: %1").arg(remaining);
            h.model.update(); // Machine uses a shared NOTIFY for unrelated readonly properties.
            QTest::qWait(20);
            QVERIFY(!h.model.paused);
            QCOMPARE(choice->property("currentIndex").toInt(), 1);
        }
        for (const auto &name : witnesses)
            QVERIFY(h.press(name));
        QVERIFY(h.press("attestExternalExtraction"));
        QCOMPARE(h.model.attests, 1);
        QCOMPARE(h.model.actions, QStringList{"focus"});
        QCOMPARE(h.model.outputStarts, 0);
        QCOMPARE(choice->property("currentIndex").toInt(), 1);
        for (const auto &name : witnesses)
            QVERIFY(!h.item(name)->property("checked").toBool());
    }
    void inhibitedStates_data() {
        QTest::addColumn<QString>("state");
        for (const auto &s : {"pending", "busy", "firing"})
            QTest::newRow(s) << QString::fromLatin1(s);
    }
    void inhibitedStates() {
        QFETCH(QString, state);
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.panel->setProperty("commissioningComplete", true);
        QVERIFY(h.press("selectAttendedExtraction"));
        for (const auto &name : witnesses)
            QVERIFY(h.press(name));
        QVERIFY(h.item("attestExternalExtraction")->isEnabled());
        QVERIFY(h.press("extractionClearanceWitness"));
        QVERIFY(h.item("confirmExtractionClearance")->isEnabled());
        h.model.pending = state == "pending";
        h.model.busy = state == "busy";
        h.model.firing = state == "firing";
        h.model.update();
        QVERIFY(!h.item("attestExternalExtraction")->isEnabled());
        QVERIFY(h.press("attestExternalExtraction", false));
        QCOMPARE(h.model.attests, 0);
        QVERIFY(!h.item("confirmExtractionClearance")->isEnabled());
        QVERIFY(h.press("revokeExternalAttendance"));
        QCOMPARE(h.model.revocations, 1);
        for (const auto &name : witnesses)
            QVERIFY(!h.item(name)->property("checked").toBool());
        QCOMPARE(h.model.outputStarts, 0);
    }
    void reasonIsPlainAndUnknownModelDoesNothing() {
        Harness h;
        QVERIFY2(h.create(true), qPrintable(h.error));
        h.model.state["reason"] = "<b>Keine Luftstrommessung</b>";
        h.model.update();
        auto *reason = h.item("extractionSupervisionReason");
        QCOMPARE(reason->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(reason->property("text").toString(), QString("<b>Keine Luftstrommessung</b>"));
        h.panel->setProperty("modelMachine", QVariant::fromValue<QObject *>(nullptr));
        QVERIFY(!h.item("selectAttendedExtraction")->isEnabled());
        QVERIFY(!h.item("confirmExtractionClearance")->isEnabled());
        QCOMPARE(h.model.selections, 0);
        QCOMPARE(h.model.attests, 0);
        QCOMPARE(h.model.outputStarts, 0);
    }
};
QTEST_MAIN(ExtractionSupervisionUiTest)
#include "extractionsupervisionui_test.moc"
