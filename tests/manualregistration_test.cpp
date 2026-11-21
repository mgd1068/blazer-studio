#include "applicationservices.h"
#include "devicemanager.h"
#include "document.h"
#include "geometry.h"
#include "machine.h"
#include "manualregistration.h"
#include "manualregistrationcontroller.h"
#include "materiallibrary.h"
#include "registration.h"
#include "reliefpreview.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <limits>

namespace {
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *result = find(child, name))
            return result;
    return nullptr;
}
bool click(QQuickWindow *window, QQuickItem *item, bool requireEnabled = true) {
    if (!item || !item->isVisible() || (requireEnabled && !item->isEnabled()))
        return false;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 1000))
        return false;
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid())
            continue;
        const auto p = item->mapToItem(parent, QPointF());
        const double delta = p.y() < 5 ? p.y() - 5
                             : p.y() + item->height() > parent->height() - 5
                                 ? p.y() + item->height() - parent->height() + 5
                                 : 0;
        parent->setProperty("contentY",
                            std::clamp(parent->property("contentY").toDouble() + delta, 0.,
                                       std::max(0., parent->property("contentHeight").toDouble() -
                                                        parent->height())));
    }
    QTest::qWait(40);
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    if (!QRect(0, 0, window->width(), window->height()).contains(point))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {}, point);
    return true;
}
bool type(QQuickWindow *window, QQuickItem *item, const QString &text) {
    if (!click(window, item))
        return false;
    item->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (const auto key : text.toLatin1())
        QTest::keyClick(window, key);
    return true;
}
} // namespace
class ManualRegistrationTest : public QObject {
    Q_OBJECT
    static void rectangle(Document &document, double x = 10, double y = 20) {
        document.addShape("rect");
        document.setProperty("x", x);
        document.setProperty("y", y);
        document.setProperty("width", 20.);
        document.setProperty("height", 10.);
        document.setLayer(1);
    }
    static QByteArray read(const QUrl &url) {
        QFile file(url.toLocalFile());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    static QVariantList rotatedPoints() { return {10., 20., 80., 40., 30., 20., 80., 60.}; }
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    }
    void realMainWorkflowAndStableProofKey_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("normal-light-mm") << false;
        QTest::newRow("compact-dark-in") << true;
    }
    void realMainWorkflowAndStableProofKey() {
        QFETCH(bool, compactDark);
        QTemporaryDir storage;
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ManualRegistrationController service(&document, &machine);
        DeviceManager devices(&document, &machine, storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));
        Registration camera;
        ApplicationServices services(&document, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding());
        QVERIFY(services.setDarkTheme(compactDark));
        document.setBackgroundPlanning(true);
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &document);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("manualRegistrationController", &service);
        context->setContextProperty("deviceManager", &devices);
        context->setContextProperty("materialLibrary", &materials);
        context->setContextProperty("cameraRegistration", &camera);
        context->setContextProperty("appServices", &services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(compactDark ? 1180 : 1480, compactDark ? 760 : 920);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](QString name) { return find(window->contentItem(), name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(name); };
        QTRY_VERIFY(object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("acknowledgeStartupSafety")));
        QTRY_VERIFY(!object("startupSafetyDialog")->property("visible").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(click(window, item("manualRegistrationButton")));
        QTRY_VERIFY(object("manualRegistrationDialog")->property("visible").toBool());
        auto editor = [&](const char *name) {
            auto *field = object(name);
            return field ? field->findChild<QQuickItem *>("numericEditor") : nullptr;
        };
        // Real raw draft survives blur and unit switch; no fallback to last committed0.
        QVERIFY(type(window, editor("registrationDesign1X"), "10 mm"));
        QVERIFY(services.setLengthUnit(compactDark ? "in" : "mm"));
        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(object("registrationDesign1X")->property("value").toDouble(), 10.);
        const QList<QPair<const char *, QString>> coordinates{
            {"registrationDesign1Y", "20 mm"}, {"registrationTarget1X", "80 mm"},
            {"registrationTarget1Y", "40 mm"}, {"registrationDesign2X", "30 mm"},
            {"registrationDesign2Y", "20 mm"}, {"registrationTarget2X", "80 mm"},
            {"registrationTarget2Y", "60 mm"}};
        for (const auto &entry : coordinates) {
            QVERIFY(type(window, editor(entry.first), entry.second));
            QTest::keyClick(window, Qt::Key_Tab);
        }
        auto *review = item("reviewRegistration");
        QVERIFY(review);
        for (const auto &bad : QStringList{"NaN", "-1 mm", "9999 mm", ""}) {
            QVERIFY(type(window, editor("registrationTarget1X"), bad));
            QVERIFY(!review->isEnabled());
            QVERIFY(click(window, review, false));
            QCOMPARE(object("registrationTarget1X")->property("inputText").toString(), bad);
            QVERIFY(!document.manualRegistrationEnabled());
        }
        QVERIFY(type(window, editor("registrationTarget1X"), "80 mm"));
        QVERIFY(review->isEnabled());
        QVERIFY(click(window, review));
        QTRY_VERIFY(service.review()["valid"].toBool());
        auto *consent = item("registrationConsent");
        QVERIFY(consent);
        auto *indicator = consent->property("indicator").value<QQuickItem *>();
        QVERIFY(indicator);
        QVERIFY(indicator->mapToItem(consent, QPointF(indicator->width() / 2, 0)).x() <
                consent->width() / 2);
        QVERIFY(click(window, consent));
        const auto staleToken = service.review()["token"].toString();
        QVERIFY(type(window, editor("registrationTarget2Y"), "60.1 mm"));
        QVERIFY(!consent->property("checked").toBool());
        QVERIFY(!item("applyRegistration")->isEnabled());
        QVERIFY(!service.applyReview(staleToken));
        QVERIFY(type(window, editor("registrationTarget2Y"), "60 mm"));
        QVERIFY(click(window, review));
        QTRY_VERIFY(service.review()["valid"].toBool());
        QVERIFY(click(window, consent));
        QVERIFY(click(window, item("applyRegistration")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(document.manualRegistrationSummary()["valid"].toBool());
        QVERIFY(document.job().encodedProgram().payload().contains("G1 X80.000 Y60.000"));
        const auto firstKey = window->property("parameterKey").toString();
        window->setProperty("proofConfirmed", true);
        machine.jog(1, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QCOMPARE(window->property("parameterKey").toString(), firstKey);
        QVERIFY(window->property("proofConfirmed").toBool());
        QVERIFY(type(window, editor("registrationTarget2Y"), "80 mm"));
        QVERIFY(click(window, item("registrationScaleChoice")));
        QVERIFY(click(window, review));
        QTRY_VERIFY(service.review()["valid"].toBool());
        QCOMPARE(service.review()["scale"].toDouble(), 2.);
        QVERIFY(click(window, item("registrationConsent")));
        QVERIFY(click(window, item("applyRegistration")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(window->property("parameterKey").toString() != firstKey);
        QVERIFY(!window->property("proofConfirmed").toBool());
        QCOMPARE(document.job().workLength, 120.);
        const auto scaleKey = window->property("parameterKey").toString();
        window->setProperty("proofConfirmed", true);
        machine.jog(1, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QCOMPARE(window->property("parameterKey").toString(), scaleKey);
        QVERIFY(window->property("proofConfirmed").toBool());
        const auto evidence = qEnvironmentVariable("BLAZER_PRINTCUT_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(click(window, item("resetRegistration")));
            // Restore the same scaling through the real controls for an active-state capture.
            QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
            QVERIFY(click(window, review));
            QVERIFY(click(window, item("registrationConsent")));
            QVERIFY(click(window, item("applyRegistration")));
            QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
            QTest::qWait(60);
            QVERIFY(window->grabWindow().save(evidence + "/registration-" +
                                              QTest::currentDataTag() + ".png"));
        }
        QVERIFY(QMetaObject::invokeMethod(object("manualRegistrationDialog"), "close"));
        QVERIFY(click(window, item("workflow1")));
        auto *workspace = qobject_cast<Workspace *>(item("workspace"));
        QVERIFY(workspace);
        QTRY_VERIFY(workspace->preview());
        QCOMPARE(workspace->document(), &document);
        QCOMPARE(document.job().workLength, 120.);
        if (!evidence.isEmpty()) {
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(evidence + "/registered-main-preview-" +
                                              QTest::currentDataTag() + ".png"));
        }
        QVERIFY(click(window, item("manualRegistrationButton")));
        QTRY_VERIFY(object("manualRegistrationDialog")->property("visible").toBool());
        document.setProperty("x", 11.);
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(!document.job().valid());
        QVERIFY(
            item("manualRegistrationIndicator")->property("text").toString().contains("GESPERRT"));
        QVERIFY(click(window, item("resetRegistration")));
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(!document.manualRegistrationEnabled());
        QVERIFY(document.job().valid());
    }
    void rigidLiteralGeometryAndOutput() {
        JobInput input;
        JobPlan plan;
        plan.moves = {{{0, 0}, {10, 20}, 3000, 0, 1, true},
                      {{10, 20}, {30, 20}, 600, 40, 1, false},
                      {{30, 20}, {30, 30}, 700, 50, 1, false},
                      {{30, 30}, {10, 30}, 800, 60, 1, false},
                      {{10, 30}, {10, 20}, 900, 70, 1, false}};
        const QByteArray frame(64, 'a');
        ManualRegistration::Intent intent;
        intent.source = ManualRegistration::SourceSnapshot::capture(input, plan, frame);
        intent.parameters = {
            {10, 20}, {30, 20}, {80, 40}, {80, 60}, ManualRegistration::Scaling::Rigid, .5};
        plan.cacheCommands(); // A cached absolute program must not leak into registered output.
        const auto result = ManualRegistration::apply(input, plan, intent, frame);
        QVERIFY2(result.valid(), qPrintable(result.summary.value("reason").toString()));
        QCOMPARE(result.plan.moves[0].to, QPointF(80, 40));
        QCOMPARE(result.plan.moves[1].to, QPointF(80, 60));
        QCOMPARE(result.plan.moves[2].to, QPointF(70, 60));
        QCOMPARE(result.plan.moves[3].to, QPointF(70, 40));
        QCOMPARE(result.plan.moves[4].to, QPointF(80, 40));
        QCOMPARE(result.plan.moves[0].power, 0.);
        for (int i = 0; i < plan.moves.size(); ++i) {
            QCOMPARE(result.plan.moves[i].power, plan.moves[i].power);
            QCOMPARE(result.plan.moves[i].speed, plan.moves[i].speed);
        }
        QVERIFY(result.plan.gcode().contains("G0 X80.000 Y40.000"));
        QVERIFY(result.plan.gcode().join('\n').contains("G1 X80.000 Y60.000"));
    }
    void scaleResidualBoundsCancellationAndStaleSnapshots() {
        JobInput input;
        JobPlan plan;
        plan.moves = {
            {{0, 0}, {10, 20}, 3000, 0, 1, true},
            {{10, 20}, {30, 20}, 600, 40, 1, false},
            {{30, 20}, {40, 20}, 600, 0, 1, false}}; // Real zero-power overscan beyond graphics.
        const QByteArray frame(64, 'a');
        ManualRegistration::Intent intent{
            ManualRegistration::SourceSnapshot::capture(input, plan, frame),
            {{10, 20}, {30, 20}, {50, 40}, {90, 40}, ManualRegistration::Scaling::Uniform, .5}};
        auto result = ManualRegistration::apply(input, plan, intent, frame);
        QVERIFY(result.valid());
        QCOMPARE(result.summary["scale"].toDouble(), 2.);
        QCOMPARE(result.plan.moves[1].to, QPointF(90, 40));
        QCOMPARE(result.plan.moves[2].to, QPointF(110, 40));
        QCOMPARE(result.plan.moves[1].speed, 600.);
        QCOMPARE(result.plan.moves[1].power, 40.);
        QCOMPARE(result.plan.workLength, 40.);
        QCOMPARE(result.summary["motionBounds"].toMap()["width"].toDouble(), 60.);
        intent.parameters.scaling = ManualRegistration::Scaling::Rigid;
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame).valid());
        intent.parameters.targetSecond = {70.25, 40};
        result = ManualRegistration::apply(input, plan, intent, frame);
        QVERIFY(result.valid());
        QCOMPARE(result.summary["residualMm"].toDouble(), .25);
        QCOMPARE(result.plan.moves[1].to,
                 QPointF(70, 40)); // First target exact; no hidden scaling.
        intent.parameters.targetFirst = {175, 20};
        intent.parameters.targetSecond = {195, 20};
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame)
                     .valid()); // Overscan205, not graphic195.
        intent.parameters.targetFirst = {50, 40};
        intent.parameters.targetSecond = {70, 40};
        QVERIFY(!ManualRegistration::apply(input, plan, intent, QByteArray(64, 'b')).valid());
        auto changed = plan;
        auto move = changed.moves[1];
        move.power = 39;
        changed.moves.replace(1, move);
        QVERIFY(!ManualRegistration::apply(input, changed, intent, frame).valid());
        auto changedInput = input;
        changedInput.cutSelectedGraphics = true;
        changedInput.selectedIds = {"different"};
        QVERIFY(!ManualRegistration::apply(changedInput, plan, intent, frame).valid());
        auto flag = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame, {}, flag).valid());
        for (const auto bad : {0., -1., std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            intent.parameters.targetSecond = {50 + bad, 40};
            QVERIFY(!ManualRegistration::apply(input, plan, intent, frame).valid());
        }
    }
    void exactGeometryProfileAndKnownApproachBindings() {
        JobInput input;
        Artwork marker;
        marker.id = "mark";
        marker.kind = "path";
        marker.toolLayer = 1;
        marker.path.moveTo(0, 0);
        marker.path.lineTo(1, 0);
        marker.bounds = {10, 20, 20, 10};
        input.items.append(marker);
        JobPlan plan;
        plan.moves = {{{0, 0}, {10, 20}, 3000, 0, 1, true},
                      {{10, 20}, {30, 20}, 600, 40, 1, false}};
        const QByteArray frame(64, 'a');
        ManualRegistration::Intent intent{
            ManualRegistration::SourceSnapshot::capture(input, plan, frame),
            {{10, 20}, {30, 20}, {80, 40}, {80, 60}, ManualRegistration::Scaling::Rigid, .5}};
        QVERIFY(intent.source.valid());
        auto changed = input;
        changed.items[0].path.setElementPositionAt(1, 1.000000000001, 0);
        QVERIFY(!ManualRegistration::apply(changed, plan, intent, frame)
                     .valid()); // Same bbox; exact path change.
        JobPlacement::PositionSnapshot head{
            {100, 75}, true, input.deviceProfile.outputFingerprint(), QByteArray(64, 'b'), 1};
        auto result = ManualRegistration::apply(input, plan, intent, frame, head);
        QVERIFY(result.valid());
        QCOMPARE(result.plan.moves[0].from, QPointF(100, 75));
        QCOMPARE(result.plan.moves[0].to, QPointF(80, 40));
        const auto key = ManualRegistration::intentFingerprint(intent);
        head.point = {101, 75};
        ++head.revision;
        auto movedHead = ManualRegistration::apply(input, plan, intent, frame, head);
        QVERIFY(movedHead.valid());
        QCOMPARE(ManualRegistration::intentFingerprint(intent), key);
        QVERIFY(result.plan.motionFingerprint() != movedHead.plan.motionFingerprint());
        head.point = {-1e-12, 75};
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame, head).valid());
        head.point = {201, 75};
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame, head).valid());
        head.point = {100, 75};
        head.known = false;
        QVERIFY(!ManualRegistration::apply(input, plan, intent, frame, head).valid());
        auto json = input.deviceProfile.toJson();
        auto workspace = json["workspace"].toObject();
        workspace["width"] = 201;
        json["workspace"] = workspace;
        QString error;
        auto otherProfile = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY(otherProfile);
        changed = input;
        changed.deviceProfile = *otherProfile;
        QVERIFY(!ManualRegistration::apply(changed, plan, intent, frame).valid());
        workspace["width"] = 2001;
        json["workspace"] = workspace;
        const auto tooWide = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        QVERIFY(tooWide);
        changed = input;
        changed.deviceProfile = *tooWide;
        JobPlan widePlan(*tooWide);
        widePlan.moves = plan.moves;
        QVERIFY(!ManualRegistration::SourceSnapshot::capture(changed, widePlan, frame).valid());
        changed = input;
        changed.items.resize(Geometry::objectLimit + 1);
        QVERIFY(ManualRegistration::geometryFingerprint(changed).isEmpty());
    }
    void lifecycleResetNoticeSurvivesFinalSuccessMessages_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("open") << 0;
        QTest::newRow("restore") << 1;
        QTest::newRow("template") << 2;
        QTest::newRow("new") << 3;
        QTest::newRow("demo") << 4;
    }
    void lifecycleResetNoticeSurvivesFinalSuccessMessages() {
        QFETCH(int, operation);
        QTemporaryDir storage;
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ManualRegistrationController service(&document, &machine);
        const auto bytes = document.projectBytes();
        const auto file = QUrl::fromLocalFile(storage.filePath("source.blazer"));
        QVERIFY(document.save(file));
        const auto prototype = QUrl::fromLocalFile(storage.filePath("source.blazertemplate"));
        QVERIFY(document.saveTemplate(prototype));
        const auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        QVERIFY(service.applyReview(review["token"].toString()));
        const auto key = document.manualRegistrationOutputKey();
        QVERIFY(!document.open(QUrl::fromLocalFile(storage.filePath("absent.blazer"))));
        QCOMPARE(document.manualRegistrationOutputKey(), key);
        QVERIFY(document.job().valid());
        if (operation == 0)
            QVERIFY(document.open(file));
        else if (operation == 1)
            QVERIFY(document.restoreBytes(bytes));
        else if (operation == 2)
            QVERIFY(document.newFromTemplate(prototype));
        else if (operation == 3)
            document.newProject();
        else
            document.demo();
        QVERIFY(!document.manualRegistrationEnabled());
        QVERIFY2(document.message().startsWith("Print-and-Cut-Registrierung entfernt"),
                 qPrintable(document.message()));
    }
    void activatingFromRelativeIsExplicitAndUndoRestoresIntentOnly() {
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        ManualRegistrationController service(&document, &machine);
        auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        QVERIFY(service.applyReview(review["token"].toString()));
        QCOMPARE(document.startMode(), Document::AbsoluteCoordinates);
        QVERIFY(document.manualRegistrationEnabled());
        document.undo();
        QCOMPARE(document.startMode(), Document::CurrentPosition);
        QVERIFY(!document.manualRegistrationEnabled());
    }
    void documentOfflineExportNoHeadSourceInvalidationAndNoPersistence() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        machine.toggleConnection(); // Disconnected Simulator must not rehydrate a head snapshot.
        document
            .clearPreviewPosition(); // Absolute offline export requires no known simulator head.
        ManualRegistrationController service(&document, &machine);
        const auto original = document.projectBytes();
        const auto originalProgram = document.job().encodedProgram().payload();
        auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY2(review["valid"].toBool(), qPrintable(review["reason"].toString()));
        QVERIFY(!review["approachKnown"].toBool());
        QVERIFY(!document.manualRegistrationEnabled());
        QVERIFY(!service.applyReview("wrong-token"));
        QVERIFY(service.applyReview(review["token"].toString()));
        QVERIFY(!service.applyReview(review["token"].toString()));
        QVERIFY(document.manualRegistrationEnabled());
        QVERIFY(document.job().valid());
        QCOMPARE(document.projectBytes(),
                 original); // Runtime mapping never leaks into project intent.
        QCOMPARE(document.items()[0].bounds, QRectF(10, 20, 20, 10));
        const auto program = document.job().encodedProgram().payload();
        QVERIFY(program != originalProgram);
        QVERIFY(program.contains("G0 X80.000 Y40.000"));
        QVERIFY(program.contains("G1 X80.000 Y60.000"));
        const auto output = QUrl::fromLocalFile(storage.filePath("registered.gcode"));
        QVERIFY(document.exportGcode(output));
        QCOMPARE(read(output), program);
        const auto frame = document.framingBounds("job");
        QVERIFY(frame["valid"].toBool());
        QCOMPARE(frame["x"].toDouble(), 70.);
        QCOMPARE(frame["y"].toDouble(), 40.);
        QCOMPARE(frame["width"].toDouble(), 10.);
        QCOMPARE(frame["height"].toDouble(), 20.);
        QVERIFY(!document.framingBounds("selection")["valid"].toBool());
        QVERIFY(!document.exportMaterialTest(output, 1000, 2000, 10, 20, 2));
        QCOMPARE(read(output), program);
        QVERIFY(!document.batch(QUrl(), QUrl(), "job"));
        QCOMPARE(read(output), program);
        Document other;
        QVERIFY(other.restoreBytes(original));
        QVERIFY(!other.manualRegistrationEnabled());
        QCOMPARE(other.job().encodedProgram().payload(), originalProgram);
        const auto saved = QUrl::fromLocalFile(storage.filePath("source.blazer"));
        QVERIFY(document.save(saved));
        QVERIFY(other.open(saved));
        QVERIFY(!other.manualRegistrationEnabled());
        document.setProperty("x", 11.);
        QVERIFY(document.manualRegistrationEnabled());
        QVERIFY(!document.job().valid());
        QVERIFY(!document.exportGcode(output));
        QCOMPARE(read(output), program); // No absolute fallback/partial replacement.
        document.undo();
        QVERIFY(!document.manualRegistrationEnabled());
        QCOMPARE(document.items()[0].bounds, QRectF(10, 20, 20, 10));
        QVERIFY(document.message().contains("Verlaufswechsel"));
    }
    void reviewTokensGroupsAndSelectionMaskCannotRetarget() {
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ManualRegistrationController service(&document, &machine);
        const auto reference = service.selectionReference();
        QVERIFY(reference["valid"].toBool());
        QCOMPARE(reference["x"].toDouble(), 20.);
        QCOMPARE(reference["y"].toDouble(), 25.);
        auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        const auto sameSource = document.projectBytes();
        QVERIFY(document.restoreBytes(sameSource)); // Same bytes, new document-frame lifetime.
        QVERIFY(service.review().isEmpty());
        QVERIFY(!service.applyReview(review["token"].toString()));
        review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        document.setProperty("x", 10.000001);
        QVERIFY(service.review().isEmpty());
        QVERIFY(!service.applyReview(review["token"].toString()));
        document.setProperty("x", 10.);
        review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        document.setCutSelectedGraphics(true);
        QVERIFY(!service.applyReview(review["token"].toString()));
        review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        QVERIFY(service.applyReview(review["token"].toString()));
        document.select(-1);
        QVERIFY(!document.job().valid());
        QVERIFY(service.resetAlignment());
        QVERIFY(!document.manualRegistrationEnabled());
        document.setCutSelectedGraphics(false);
        document.select(0);
        QVERIFY(document.setToolLayer(1));
        const auto helper = service.selectionReference();
        QVERIFY(helper["valid"].toBool());
        QVERIFY(!document.job().valid()); // Reference tool was not silently promoted to output.
        rectangle(document, 50, 30);
        QVERIFY(document.setToolLayer(2));
        QVERIFY(!document.job().valid());
        QVERIFY(service.selectionReference()["valid"].toBool());
        auto bad = rotatedPoints();
        bad[0] = QString("10");
        QVERIFY(!service.reviewPoints(bad, false, .5)["valid"].toBool());
    }
    void actualSimulatorConsumesRegisteredPlanAndFreezesWhileRunning() {
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ManualRegistrationController service(&document, &machine);
        auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        QVERIFY(service.applyReview(review["token"].toString()));
        const auto start = QPointF(machine.x(), machine.y());
        QCOMPARE(start, QPointF(100, 75));
        const auto planned = document.job();
        QCOMPARE(planned.moves.first().from, start);
        QCOMPARE(planned.moves.first().to, QPointF(80, 40));
        machine.start();
        QVERIFY(machine.running());
        QTRY_VERIFY(machine.progress() > .005);
        const auto current = QPointF(machine.x(), machine.y());
        QVERIFY(current != start);
        QVERIFY(current.x() >= 80 && current.x() <= 100);
        QVERIFY(std::abs((current.x() - 100) * (-35) - (current.y() - 75) * (-20)) < 1e-5);
        QVERIFY(!service.blockReason().isEmpty());
        QVERIFY(!service.resetAlignment());
        document.setProperty(
            "x", 11.); // Next design can change; running Simulator retains its copied plan.
        const auto before = QPointF(machine.x(), machine.y());
        QTest::qWait(80);
        const auto after = QPointF(machine.x(), machine.y());
        QVERIFY(before != after);
        QVERIFY(std::abs((after.x() - 100) * (-35) - (after.y() - 75) * (-20)) < 1e-5);
        machine.stop();
        QVERIFY(!document.job().valid());
    }
    void backgroundAndCancelledOldSourcesNeverPublishAbsoluteFallback() {
        Document document;
        document.newProject();
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ManualRegistrationController service(&document, &machine);
        const auto review = service.reviewPoints(rotatedPoints(), false, .5);
        QVERIFY(review["valid"].toBool());
        QVERIFY(service.applyReview(review["token"].toString()));
        const auto sync = document.job().encodedProgram().payload();
        document.setBackgroundPlanning(true);
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(document.job().valid());
        QCOMPARE(document.job().encodedProgram().payload(), sync);
        std::atomic_bool started = false;
        JobPreparation preparation(nullptr, [&](const JobInput &input, const Cancellation &cancel) {
            if (!started.exchange(true))
                while (!isCancelled(cancel))
                    QThread::msleep(1);
            return JobPlanner::build(input, cancel);
        });
        preparation.submit(document.planningInput());
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 3000);
        document.setProperty("x", 11.);
        preparation.submit(document.planningInput());
        QTRY_VERIFY_WITH_TIMEOUT(!preparation.busy(), 5000);
        QVERIFY(!preparation.plan().valid());
        QVERIFY(preparation.plan().errors.join(" ").contains("veraltet"));
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(!document.job().valid());
        document.cancelPlanning();
        document.retryPlanning();
        QTRY_VERIFY_WITH_TIMEOUT(!document.planningBusy(), 5000);
        QVERIFY(!document.job().valid());
    }
};
QTEST_MAIN(ManualRegistrationTest)
#include "manualregistration_test.moc"
