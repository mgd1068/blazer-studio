#include "applicationservices.h"
#include "document.h"
#include "materiallibrary.h"
#include "recovery.h"
#include "reliefpreview.h"
#include "simulator.h"
#include "workspace.h"
#include <QFile>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <functional>

class UiMachine : public Simulator {
    Q_OBJECT
    Q_PROPERTY(bool simulation READ simulation WRITE setSimulation NOTIFY changed)
  public:
    bool simulation() const { return m_simulation; }
    void setSimulation(bool value) {
        m_simulation = value;
        emit changed();
    }

  private:
    bool m_simulation = true;
};
class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    }
    void lengthFieldsConvertSuffixesAndPreserveUneditedPrecision() {
        QTemporaryDir storage;
        Document project;
        Simulator machine;
        ApplicationServices services(&project, &machine, storage.filePath("units.ini"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("appServices", &services);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/BlazerStudio/qml/NumberField.qml"));
        std::unique_ptr<QObject> field(component.create());
        QVERIFY2(field, qPrintable(component.errorString()));
        auto *input = field->findChild<QObject *>("numericEditor");
        QVERIFY(input);
        QSignalSpy edited(field.get(), SIGNAL(edited(double)));
        field->setProperty("value", 65.);
        QVERIFY(services.setLengthUnit("in"));
        QCOMPARE(field->property("shownUnit").toString(), QString("in"));
        QCOMPARE(field->property("inputText").toString(), QString("2.5591"));
        QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        QCOMPARE(edited.count(), 0);
        QCOMPARE(field->property("value").toDouble(), 65.);
        QVariant value;
        QVERIFY(
            QMetaObject::invokeMethod(field.get(), "numericValue", Q_RETURN_ARG(QVariant, value)));
        QCOMPARE(value.toDouble(), 65.);
        const QList<QPair<QString, double>> cases{
            {"1,25 in", 31.75}, {"25.4 mm", 25.4}, {"2\"", 50.8}, {".5", 12.7}, {"1e-2 in", .254}};
        for (const auto &entry : cases) {
            field->setProperty("inputText", entry.first);
            QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
            QVERIFY(!edited.isEmpty());
            QVERIFY(qAbs(edited.takeLast()[0].toDouble() - entry.second) < 1e-10);
        }
        field->setProperty("inputText", "1/2 in");
        const auto count = edited.count();
        QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        QCOMPARE(edited.count(), count);
        QVERIFY(services.setLengthUnit("mm"));
        field->setProperty("inputText", "2.5");
        QVERIFY(QMetaObject::invokeMethod(input, "textEdited"));
        QVERIFY(services.setLengthUnit("in"));
        QCOMPARE(field->property("shownUnit").toString(), QString("mm"));
        QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        QCOMPARE(edited.takeLast()[0].toDouble(), 2.5);
        field->setProperty("unit", "mm/min");
        field->setProperty("value", 1234.);
        QVERIFY(QMetaObject::invokeMethod(field.get(), "resetInput"));
        QCOMPARE(field->property("shownUnit").toString(), QString("mm/min"));
        QCOMPARE(field->property("inputText").toString(), QString("1234.0"));
        field->setProperty("inputText", "1 in");
        QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        QVERIFY(edited.isEmpty());
        Workspace workspace;
        QSignalSpy changed(&workspace, &Workspace::imperialChanged);
        QCOMPARE(workspace.snapStep(), .5);
        workspace.setImperial(true);
        QCOMPARE(workspace.snapStep(), .635);
        QCOMPARE(changed.count(), 1);
        workspace.setImperial(true);
        QCOMPARE(changed.count(), 1);
    }
    void imperialDragSnapsInWorldUnitsAndIsOneUndoStep() {
        class TestWorkspace : public Workspace {
          public:
            using Workspace::mouseMoveEvent;
            using Workspace::mousePressEvent;
            using Workspace::mouseReleaseEvent;
        };
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 50.8);
        d.setProperty("y", 50.8);
        d.setProperty("width", 25.4);
        d.setProperty("height", 25.4);
        TestWorkspace workspace;
        workspace.setWidth(1000);
        workspace.setHeight(800);
        workspace.setDocument(&d);
        workspace.setImperial(true);
        const auto bounds = workspace.selectedScreenBounds();
        const QPointF start = bounds.center();
        const double pixelsPerMm = bounds.width() / 25.4;
        const auto before = d.projectBytes();
        const auto position = d.historyPosition();
        QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
                          Qt::AltModifier);
        workspace.mousePressEvent(&press);
        const QPointF end = start + QPointF(.27 * 25.4 * pixelsPerMm, -.08 * 25.4 * pixelsPerMm);
        QMouseEvent move(QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton,
                         Qt::AltModifier);
        workspace.mouseMoveEvent(&move);
        QMouseEvent release(QEvent::MouseButtonRelease, end, end, Qt::LeftButton, Qt::NoButton,
                            Qt::AltModifier);
        workspace.mouseReleaseEvent(&release);
        QVERIFY(qAbs(d.items()[0].bounds.x() - 57.785) < 1e-9);
        QVERIFY(qAbs(d.items()[0].bounds.y() - 52.705) < 1e-9);
        QCOMPARE(d.historyPosition(), position + 1);
        QVERIFY(d.job().valid());
        QVERIFY(d.job().gcode().contains("G21"));
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(workspace.imperial());
    }
    void objectEdgesSnapToVisibleGuidesAndAltDisablesTheTarget() {
        class TestWorkspace : public Workspace {
          public:
            using Workspace::mouseMoveEvent; using Workspace::mousePressEvent;
            using Workspace::mouseReleaseEvent;
        };
        for (int mode=0;mode<4;++mode) {
            Document d; d.newProject(); d.addShape("rect");
            d.addGuide("vertical",123.25);
            if(mode==2)d.setGuidesVisible(false);
            if(mode==3)d.setGuideSnapping(false);
            TestWorkspace w; w.setWidth(910); w.setHeight(710); w.setDocument(&d);
            const auto modifiers=mode==1 ? Qt::AltModifier : Qt::NoModifier;
            const QPointF start(415,375), end(448.6,375);
            QMouseEvent press(QEvent::MouseButtonPress,start,start,Qt::LeftButton,Qt::LeftButton,modifiers);
            QMouseEvent move(QEvent::MouseMove,end,end,Qt::NoButton,Qt::LeftButton,modifiers);
            QMouseEvent release(QEvent::MouseButtonRelease,end,end,Qt::LeftButton,Qt::NoButton,modifiers);
            w.mousePressEvent(&press);w.mouseMoveEvent(&move);w.mouseReleaseEvent(&release);
            QCOMPARE(d.items()[0].bounds.x(),mode==0 ? 73.25 : 73.5);
            QCOMPARE(d.items()[0].bounds.y(),55.);
        }
    }
    void rulersCreateMoveCancelAndLockGuidesWithoutLaserOutput() {
        class TestWorkspace : public Workspace {
          public:
            using Workspace::mouseMoveEvent;
            using Workspace::mousePressEvent;
            using Workspace::mouseReleaseEvent;
            using Workspace::mouseUngrabEvent;
        };
        Document d;
        d.newProject();
        TestWorkspace w;
        w.setWidth(910);
        w.setHeight(710);
        w.setDocument(&d);
        // Scale = 4 pixels/mm; bed origin = (55,55), Y=0 at 655.
        auto send = [&](QEvent::Type type, QPointF pos) {
            QMouseEvent e(type, pos, pos, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                          Qt::NoModifier);
            if (type == QEvent::MouseButtonPress)
                w.mousePressEvent(&e);
            else if (type == QEvent::MouseButtonRelease)
                w.mouseReleaseEvent(&e);
            else
                w.mouseMoveEvent(&e);
        };
        const auto code = d.job().gcode();
        send(QEvent::MouseButtonPress, {300, 40});
        QVERIFY(w.guideDragging());
        // Commit uses release position even when no last move event arrives.
        send(QEvent::MouseButtonRelease, {300, 355});
        QCOMPARE(d.allGuides().size(), 1);
        QCOMPARE(d.allGuides()[0].axis, QString("horizontal"));
        QCOMPARE(d.allGuides()[0].position, 75.);
        const auto id = d.allGuides()[0].id;
        const auto before = d.projectBytes();
        const auto history = d.historyPosition();
        send(QEvent::MouseButtonPress, {350, 355});
        send(QEvent::MouseMove, {350, 315});
        QCOMPARE(d.projectBytes(), before);
        send(QEvent::MouseButtonRelease, {350, 315});
        QCOMPARE(d.allGuides()[0].position, 85.);
        QCOMPARE(d.historyPosition(), history + 1);
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        send(QEvent::MouseButtonPress, {350, 355});
        send(QEvent::MouseMove, {350, 300});
        w.cancelGuideDrag();
        QVERIFY(!w.guideDragging());
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(d.setGuideLocked(id, true));
        send(QEvent::MouseButtonPress, {350, 355});
        QVERIFY(!w.guideDragging());
        send(QEvent::MouseButtonRelease, {350, 300});
        QCOMPARE(d.allGuides()[0].position, 75.);
        QVERIFY(d.setGuideLocked(id, false));
        send(QEvent::MouseButtonPress, {350, 355});
        send(QEvent::MouseButtonRelease, {10, 30});
        QVERIFY(d.guides().isEmpty());
        send(QEvent::MouseButtonPress, {30, 300});
        send(QEvent::MouseButtonRelease, {255, 300});
        QCOMPARE(d.allGuides()[0].axis, QString("vertical"));
        QCOMPARE(d.allGuides()[0].position, 50.);
        const auto vertical = d.projectBytes();
        send(QEvent::MouseButtonPress, {300, 40});
        w.mouseUngrabEvent();
        QCOMPARE(d.projectBytes(), vertical);
        QCOMPARE(d.job().gcode(), code);
    }
    void editNavigateAndFocus() {
        Document project;
        QTemporaryDir storage;
        MaterialLibrary library(storage.filePath("materials.json"));
        UiMachine machine;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        Registration cameraRegistration;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("project", &project);
        engine.rootContext()->setContextProperty("appServices", &services);
        engine.rootContext()->setContextProperty("cameraRegistration", &cameraRegistration);
        engine.rootContext()->setContextProperty("materialLibrary", &library);
        engine.rootContext()->setContextProperty("machine", &machine);
        QFile f(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/blaze-x-m3.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        engine.rootContext()->setContextProperty("deviceProfile",
                                                 QJsonDocument::fromJson(f.readAll()).toVariant());
        QFile reference(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/manufacturer-materials.json"));
        QVERIFY(reference.open(QIODevice::ReadOnly));
        engine.rootContext()->setContextProperty(
            "manufacturerReference", QJsonDocument::fromJson(reference.readAll()).toVariant());
        engine.load(QUrl::fromLocalFile(QStringLiteral(BLAZER_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(w);
        QCOMPARE(w->property("basic").toBool(), true);
        auto originalBytes = project.projectBytes();
        auto *intro = w->findChild<QObject *>("onboardingDialog");
        QVERIFY(intro);
        QTRY_VERIFY(intro->property("visible").toBool());
        // Hide temporarily for existing layout screenshots; restore before the onboarding tests.
        QVERIFY(QMetaObject::invokeMethod(intro, "close"));
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("BLAZER_BASIC_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_BASIC_SCREENSHOT")));
        w->resize(1180, 760);
        w->setProperty("night", true);
        QTest::qWait(100);
        if (qEnvironmentVariableIsSet("BLAZER_BASIC_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_BASIC_SCREENSHOT") +
                                         ".compact.png"));
        w->resize(1480, 920);
        w->setProperty("night", false);
        w->setProperty("basic", false);
        QCOMPARE(project.projectBytes(), originalBytes);
        QVERIFY(QTest::qWaitForWindowExposed(w));
        auto item = [w](const char *name) {
            std::function<QQuickItem *(QQuickItem *)> visit =
                [&](QQuickItem *node) -> QQuickItem * {
                if (node->objectName() == QString::fromLatin1(name))
                    return node;
                for (auto *child : node->childItems())
                    if (auto *found = visit(child))
                        return found;
                return nullptr;
            };
            return visit(w->contentItem());
        };
        auto click = [w](QQuickItem *i) {
            QTest::qWait(50);
            for (auto *parent = i->parentItem(); parent; parent = parent->parentItem()) {
                if (!parent->property("contentY").isValid())
                    continue;
                const auto point = i->mapToItem(parent, QPointF());
                double delta = point.y() < 5 ? point.y() - 5
                               : point.y() + i->height() > parent->height() - 5
                                   ? point.y() + i->height() - parent->height() + 5
                                   : 0;
                const auto limit =
                    std::max(0., parent->property("contentHeight").toDouble() - parent->height());
                parent->setProperty(
                    "contentY",
                    std::clamp(parent->property("contentY").toDouble() + delta, 0., limit));
            }
            QTest::qWait(30);
            QTest::mouseClick(w, Qt::LeftButton, {},
                              i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint());
        };
        QVERIFY(QMetaObject::invokeMethod(intro, "open"));
        QTRY_VERIFY(intro->property("visible").toBool());
        if (qEnvironmentVariableIsSet("BLAZER_BETA_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_BETA_SCREENSHOT") +
                                         ".intro.png"));
        click(item("onboardingNext"));
        QCOMPARE(intro->property("step").toInt(), 1);
        click(item("onboardingNext"));
        QCOMPARE(intro->property("step").toInt(), 2);
        QTest::qWait(50);
        QVERIFY(item("onboardingFinish")->width() >= item("onboardingFinish")->implicitWidth());
        QVERIFY(item("onboardingPractice")->width() >= item("onboardingPractice")->implicitWidth());
        click(item("onboardingPractice"));
        QTRY_VERIFY(!intro->property("visible").toBool());
        QVERIFY(!services.firstRun());
        QVERIFY(machine.simulation());
        QVERIFY(!machine.busy());
        QVERIFY(project.count() > 0);
        QVERIFY(!project.dirty());
        auto *betaPopup = w->findChild<QObject *>("betaHelpDialog");
        QVERIFY(betaPopup);
        machine.focus(true);
        QVERIFY(machine.firing());
        click(item("betaHelpButton"));
        QVERIFY(!machine.firing());
        QTRY_VERIFY(betaPopup->property("visible").toBool());
        QTest::qWait(50);
        if (qEnvironmentVariableIsSet("BLAZER_BETA_SCREENSHOT"))
            QVERIFY(
                w->grabWindow().save(qEnvironmentVariable("BLAZER_BETA_SCREENSHOT") + ".help.png"));
        if (qEnvironmentVariableIsSet("BLAZER_BETA_SCREENSHOT")) {
            w->resize(1180, 760);
            w->setProperty("night", true);
            QTest::qWait(100);
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_BETA_SCREENSHOT") +
                                         ".help-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(betaPopup, "close"));
            QVERIFY(QMetaObject::invokeMethod(intro, "open"));
            intro->setProperty("step", 2);
            QTest::qWait(100);
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_BETA_SCREENSHOT") +
                                         ".intro-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(intro, "close"));
            w->resize(1480, 920);
            w->setProperty("night", false);
        }
        QVERIFY(QMetaObject::invokeMethod(betaPopup, "close"));
        w->setProperty("basic", false);

        // Real QML controls must stay usable while a large raster is processed.
        project.newProject();
        QImage large(2048, 2048, QImage::Format_ARGB32);
        large.fill(Qt::black);
        auto largePath = storage.filePath("background.png");
        QVERIFY(large.save(largePath));
        project.setBackgroundPlanning(true);
        QVERIFY(project.importImage(QUrl::fromLocalFile(largePath)));
        QTRY_COMPARE_WITH_TIMEOUT(project.planningState(), QString("running"), 5000);
        QVERIFY(item("cancelPlanning")->isVisible());
        QSignalSpy cancelledClick(item("cancelPlanning"), SIGNAL(clicked()));
        click(item("cancelPlanning"));
        QCOMPARE(cancelledClick.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy(), 3000);
        QVERIFY(!project.job().valid());
        QTRY_VERIFY(item("retryPlanning")->isVisible());
        click(item("retryPlanning"));
        QTRY_VERIFY_WITH_TIMEOUT(project.job().valid(), 15000);
        QVERIFY(!item("cancelPlanning")->isVisible());
        project.setBackgroundPlanning(false);
        project.demo();
        // Basic is a view mode: switching itself must never rewrite a project.
        w->setProperty("basic", true);
        QTRY_VERIFY(!item("exportGcodeButton")->isVisible());
        QVERIFY(!item("workflow3"));
        project.newProject();
        project.addShape("rect");
        project.setObjectSetting("power", 91.);
        click(item("basicProfileApply"));
        QVERIFY(project.items()[0].settings.isEmpty());
        auto *values = item("basicValues");
        QVERIFY(values && values->isVisible());
        QVERIFY(values->property("text").toString().contains("Geschwindigkeit: "));
        QVERIFY(values->property("text").toString().contains("Leistung: "));
        // Material-first and import-first workflows must yield the same active operation.
        QVariantMap engravingProfile;
        for (int i = 0; i < library.count(); ++i) {
            auto profile = library.profileAt(i);
            if (profile["moduleW"].toInt() == 5 && profile["engrave"].toMap()["enabled"].toBool() &&
                !profile["cut"].toMap()["enabled"].toBool()) {
                engravingProfile = profile;
                break;
            }
        }
        QVERIFY(!engravingProfile.isEmpty());
        QVERIFY(project.applyBasicMaterial(engravingProfile));
        QFile vector(storage.filePath("basic-motif.svg"));
        QVERIFY(vector.open(QIODevice::WriteOnly));
        vector.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2mm\" height=\"2mm\" "
                     "viewBox=\"0 0 2 2\"><rect width=\"2\" height=\"2\"/></svg>");
        vector.close();
        QVERIFY(QMetaObject::invokeMethod(
            w, "importMotif", Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(vector.fileName())))));
        QCOMPARE(project.count(), 2);
        for (const auto &art : project.items())
            QCOMPARE(art.layer, 0);
        QVERIFY(project.job().valid());
        machine.setSimulation(false); // UI state only; this fixture has no serial transport.
        w->setProperty("page", 1);
        QTRY_VERIFY(!item("startJobButton")->isEnabled());
        click(item("startJobButton"));
        QVERIFY(!machine.busy());
        click(item("basicProof"));
        QTRY_VERIFY(item("startJobButton")->isEnabled());
        project.setLayerSetting(0, "power", 21.);
        QTRY_VERIFY(!w->property("proofConfirmed").toBool());
        QTRY_VERIFY(!item("startJobButton")->isEnabled());
        click(item("basicProfileApply"));
        machine.setSimulation(true);
        w->setProperty("page", 0);
        auto *xField = item("objectXInput");
        QVERIFY(xField);
        auto *help = xField->findChild<QObject *>("helpBubble");
        QVERIFY(help);
        QVERIFY(help->property("description").toString().contains("Abstand"));
        auto *target = qobject_cast<QQuickItem *>(help->property("target").value<QObject *>());
        QVERIFY(target);
        target->forceActiveFocus(Qt::TabFocusReason);
        auto *tip = help->findChild<QObject *>("helpTooltip");
        QVERIFY(tip);
        QTRY_VERIFY_WITH_TIMEOUT(tip->property("visible").toBool(), 2000);
        QVERIFY(!tip->property("text").toString().isEmpty());
        if (qEnvironmentVariableIsSet("BLAZER_TOOLTIP_SCREENSHOT")) {
            QTest::qWait(100);
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_TOOLTIP_SCREENSHOT")));
        }
        auto beforeProbe = project.projectBytes();
        click(item("basicProbe"));
        auto *probeDialog = w->findChild<QObject *>("discardDialog");
        QVERIFY(probeDialog);
        auto *discardHelp = item("discardChangesButton")->findChild<QObject *>("helpBubble");
        QVERIFY(discardHelp);
        QVERIFY(discardHelp->property("description").toString().contains("ungespeicherten"));
        QTRY_VERIFY(probeDialog->property("visible").toBool());
        click(item("cancelCloseButton"));
        QCOMPARE(project.projectBytes(), beforeProbe);
        auto profileBeforeProbe = project.material();
        click(item("basicProbe"));
        click(item("discardChangesButton"));
        QTRY_COMPARE(project.count(), 1);
        QCOMPARE(project.material(), profileBeforeProbe);
        QCOMPARE(project.selection()["width"].toDouble(), 5.);
        QCOMPARE(project.selection()["height"].toDouble(), 5.);
        QCOMPARE(project.items()[0].layer, 0);
        QVERIFY(w->property("isBasicProbe").toBool());
        QVERIFY(!machine.busy());
        QCOMPARE(w->property("page").toInt(), 1);
        QVERIFY(item("startJobButton")->property("text").toString().contains("Bildschirm"));
        QVERIFY(item("startJobButton")->isEnabled());
        machine.toggleConnection();
        QTRY_VERIFY(!item("startJobButton")->isEnabled());
        machine.toggleConnection();
        click(item("basicProof"));
        QVERIFY(w->property("proofConfirmed").toBool());
        project.setLayerSetting(0, "power", 21.);
        QTRY_VERIFY(!w->property("proofConfirmed").toBool());
        QVERIFY(!w->property("isBasicProbe").toBool());
        project.demo();
        w->setProperty("page", 0);
        const auto beforeModeSwitch = project.projectBytes();
        click(item("modeToggle"));
        QTRY_VERIFY(!w->property("basic").toBool());
        QCOMPARE(project.projectBytes(), beforeModeSwitch);
        QTRY_VERIFY(item("workflow3"));
        if (qEnvironmentVariableIsSet("BLAZER_ADVANCED_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_ADVANCED_SCREENSHOT")));
        auto *manufacturer = item("manufacturerButton");
        QVERIFY(manufacturer);
        click(manufacturer);
        auto *dialog = w->findChild<QObject *>("manufacturerDialog");
        QVERIFY(dialog);
        QVERIFY(dialog->property("visible").toBool());
        auto *catalogRows = item("manufacturerRows");
        QVERIFY(catalogRows);
        QCOMPARE(catalogRows->property("count").toInt(), 15);
        QTest::keyClick(w, Qt::Key_Escape);
        QTest::qWait(200);
        auto *choice = item("materialChoice");
        QVERIFY(choice);
        auto *head = item("toolHeadChoice");
        QVERIFY(head);
        QCOMPARE(w->property("selectedModuleW").toDouble(), 5.);
        QCOMPARE(choice->property("count").toInt(), 27);
        auto chooseHead = [&](Qt::Key position) {
            click(head);
            QTest::keyClick(w, position);
            QTest::keyClick(w, Qt::Key_Return);
        };
        const auto initialMaterial = project.material();
        chooseHead(Qt::Key_End);
        QTRY_COMPARE(w->property("selectedModuleW").toDouble(), 10.);
        QTRY_COMPARE(choice->property("count").toInt(), 25);
        QCOMPARE(project.material(), initialMaterial);
        project.setProperty("x", 31.);
        QCOMPARE(w->property("selectedModuleW").toDouble(), 10.);
        QTRY_COMPARE(choice->property("currentIndex").toInt(), -1);
        click(choice);
        QTest::keyClick(w, Qt::Key_Home);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.material()["moduleW"].toInt(), 10);
        chooseHead(Qt::Key_Home);
        QTRY_COMPARE(w->property("selectedModuleW").toDouble(), 1.);
        QTRY_COMPARE(choice->property("count").toInt(), 13);
        QCOMPARE(project.material()["moduleW"].toInt(), 10);
        click(choice);
        QCOMPARE(project.material()["moduleW"].toInt(), 10);
        auto *popup = w->findChild<QObject *>("materialPopup");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QVERIFY(popup->property("width").toDouble() > choice->width());
        auto verifyMaterialText = [&]() {
            QList<QQuickItem *> texts;
            std::function<void(QQuickItem *)> collect = [&](QQuickItem *node) {
                if (node->objectName() == "materialOptionText")
                    texts.append(node);
                for (auto *child : node->childItems())
                    collect(child);
            };
            collect(w->contentItem());
            QVERIFY(!texts.isEmpty());
            for (auto *text : texts) {
                if (text->isVisible()) {
                    QVERIFY(!text->property("truncated").toBool());
                    QVERIFY(text->width() > 0);
                }
            }
        };
        QTest::qWait(80);
        verifyMaterialText();
        if (qEnvironmentVariableIsSet("BLAZER_MATERIAL_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_MATERIAL_SCREENSHOT")));
        w->resize(1180, 760);
        w->setProperty("night", true);
        QTest::qWait(80);
        verifyMaterialText();
        QVERIFY(popup->property("x").toDouble() + popup->property("width").toDouble() <=
                w->width());
        if (qEnvironmentVariableIsSet("BLAZER_MATERIAL_SCREENSHOT"))
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_MATERIAL_SCREENSHOT") +
                                         ".compact.png"));
        w->resize(1480, 920);
        w->setProperty("night", false);
        QTest::keyClick(w, Qt::Key_End);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.material()["moduleW"].toInt(), 1);
        QCOMPARE(project.material()["sourceParameters"].toMap()["material"].toString(), "Kork");
        QVERIFY(project.layers()[0].toMap()["enabled"].toBool());
        QVERIFY(!project.layers()[1].toMap()["enabled"].toBool());
        click(head);
        QTest::keyClick(w, Qt::Key_Home);
        QTest::keyClick(w, Qt::Key_Down);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(w->property("selectedModuleW").toDouble(), 5.);
        QTRY_COMPARE(choice->property("count").toInt(), 27);
        click(choice);
        QTest::keyClick(w, Qt::Key_Home);
        QTest::keyClick(w, Qt::Key_Down);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.layers()[1].toMap()["speed"].toDouble(), 250.);
        QCOMPARE(project.material()["id"].toString(), "vendor-copy-pro5w-plywood-3");
        auto *selectedText = choice->property("contentItem").value<QQuickItem *>();
        QVERIFY(selectedText);
        QVERIFY(!selectedText->property("truncated").toBool());
        QTRY_VERIFY(choice->height() > 38);
        auto *edit = item("editMaterialButton");
        QVERIFY(edit);
        click(edit);
        auto *editor = w->findChild<QObject *>("materialEditor");
        QVERIFY(editor);
        QTRY_VERIFY(editor->property("visible").toBool());
        if (qEnvironmentVariableIsSet("BLAZER_EDITOR_SCREENSHOT")) {
            QTest::qWait(150);
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_EDITOR_SCREENSHOT")));
        }
        auto *name = item("materialNameInput");
        QVERIFY(name);
        click(name);
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        for (char c : QByteArray("Eigenes Testmaterial"))
            QTest::keyClick(w, c);
        auto *power = item("materialCutPower");
        QVERIFY(power);
        power->setProperty("inputText", "42,5");
        auto *save = item("saveMaterialButton");
        QVERIFY(save);
        click(save);
        QTRY_VERIFY(!editor->property("visible").toBool());
        QCOMPARE(project.material()["name"].toString(), "Eigenes Testmaterial");
        QCOMPARE(project.layers()[1].toMap()["power"].toDouble(), 42.5);
        QTRY_COMPARE(head->property("currentIndex").toInt(), 1);
        QTRY_COMPARE(choice->property("currentIndex").toInt(), 1);
        MaterialLibrary reopened(storage.filePath("materials.json"));
        QCOMPARE(reopened.profileAt(1)["name"].toString(), "Eigenes Testmaterial");
        click(edit);
        power = item("materialCutPower");
        power->setProperty("inputText", "ungültig");
        click(save);
        QVERIFY(editor->property("visible").toBool());
        QVERIFY(!item("materialEditorError")->property("text").toString().isEmpty());
        power->setProperty("inputText", "37.5");
        QTest::keyClick(w, Qt::Key_Escape);
        QCOMPARE(project.layers()[1].toMap()["power"].toDouble(), 42.5);
        click(edit);
        QCOMPARE(item("materialCutPower")->property("inputText").toString(), "42.5");
        QTest::keyClick(w, Qt::Key_Escape);
        project.demo();
        auto *list = item("objectList");
        QVERIFY(list);
        auto clickObject = [&](int row) {
            QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row),
                                              Q_ARG(int, 1)));
            QTest::qWait(50);
            auto *object = item(qPrintable(QString("objectRow%1").arg(row)));
            QVERIFY(object);
            // Use the name area, leaving the row's operation selector and checkbox independent.
            auto point = object->mapToScene(
                QPointF(60, object->property("artGroupFirst").toBool() ? 44 : 14));
            QTest::mouseClick(w, Qt::LeftButton, {}, point.toPoint());
        };
        clickObject(3);
        QCOMPARE(project.selected(), 3);
        auto *objectName = item("objectNameInput");
        QVERIFY(objectName);
        click(objectName);
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        for (char c : QByteArray("Mein Titel"))
            QTest::keyClick(w, c);
        clickObject(4);
        QCOMPARE(project.selected(), 4);
        QCOMPARE(project.items()[3].name, QString("Mein Titel"));
        clickObject(3);
        auto *operation = item("objectOperation3");
        QVERIFY(operation);
        click(operation);
        QTest::keyClick(w, Qt::Key_Home);
        QTest::keyClick(w, Qt::Key_Down); // Cut operation; T1/T2 follow the normal operations.
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.items()[3].layer, 1);
        QCOMPARE(project.items()[3].toolLayer,0);
        QCOMPARE(project.items()[4].layer, 0);
        auto *widthInput = item("objectWidthInput");
        QVERIFY(widthInput);
        click(widthInput);
        widthInput->setProperty("inputText", "49");
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.items()[3].bounds.width(), 49.);
        auto *xInput = item("objectXInput");
        QVERIFY(xInput);
        click(xInput);
        xInput->setProperty("inputText", "61,5");
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.items()[3].bounds.x(), 61.5);
        QVERIFY(
            QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 4), Q_ARG(int, 1)));
        QTest::qWait(50);
        auto *check = item("objectCheck4");
        QVERIFY(check);
        click(check);
        QTRY_COMPARE(project.selectedRows().size(), 2);
        auto *groupButton = item("groupObjectsButton");
        QVERIFY(groupButton);
        click(groupButton);
        QTRY_VERIFY(project.selection()["grouped"].toBool());
        auto *scale = item("objectScaleInput");
        QVERIFY(scale);
        click(scale);
        const double width = project.selectionBounds().width();
        scale->setProperty("inputText", "200");
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.selectionBounds().width(), width * 2);
        auto *canvas = qobject_cast<Workspace *>(item("workspace"));
        QVERIFY(canvas);
        QVERIFY(canvas->boundingRect().contains(canvas->selectedScreenBounds()));
        click(xInput);
        xInput->setProperty("inputText", "5");
        QTest::keyClick(w, Qt::Key_Return);
        auto *yInput = item("objectYInput");
        QVERIFY(yInput);
        click(yInput);
        yInput->setProperty("inputText", "10");
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.selectionBounds().topLeft(), QPointF(5, 10));
        click(objectName);
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        for (char c : QByteArray("Beschriftung"))
            QTest::keyClick(w, c);
        QTest::keyClick(w, Qt::Key_Return);
        QCOMPARE(project.selection()["name"].toString(), "Beschriftung");
        if (qEnvironmentVariableIsSet("BLAZER_OBJECT_SCREENSHOT")) {
            QTest::qWait(80);
            QVERIFY(w->grabWindow().save(qEnvironmentVariable("BLAZER_OBJECT_SCREENSHOT")));
        }
        const auto beforeDrag = project.selectionBounds();
        const auto child3 = project.items()[3].bounds.topLeft(),
                   child4 = project.items()[4].bounds.topLeft();
        const auto dragStart =
            canvas->mapToScene(canvas->selectedScreenBounds().center()).toPoint();
        QTest::mousePress(w, Qt::LeftButton, {}, dragStart);
        QTest::mouseMove(w, dragStart + QPoint(15, -15), 40);
        QTest::mouseRelease(w, Qt::LeftButton, {}, dragStart + QPoint(15, -15));
        QTRY_VERIFY(project.selectionBounds().topLeft() != beforeDrag.topLeft());
        QCOMPARE(project.items()[3].bounds.topLeft() - child3,
                 project.items()[4].bounds.topLeft() - child4);
        QCOMPARE(project.selectionBounds().size(), beforeDrag.size());
        auto *copy = item("copyObjectsButton");
        QVERIFY(copy);
        click(copy);
        QTRY_COMPARE(project.count(), 7);
        QCOMPARE(project.selectedRows().size(), 2);
        auto *remove = item("deleteObjectsButton");
        QVERIFY(remove);
        click(remove);
        QTRY_COMPARE(project.count(), 5);
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(project.count(), 7);
        QVERIFY(project.selection()["grouped"].toBool());
        project.demo();
        auto *add = item("addRectangle");
        QVERIFY(add);
        click(add);
        QCOMPARE(project.rowCount(), 6);
        QVERIFY(project.dirty());
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(project.rowCount(), 5);
        auto *preview = item("workflow1");
        QVERIFY(preview);
        click(preview);
        QCOMPARE(w->property("page").toInt(), 1);
        auto *device = item("workflow2");
        QVERIFY(device);
        click(device);
        QCOMPARE(w->property("page").toInt(), 2);
        QTest::qWait(60);
        auto *focus = item("focusHold");
        QVERIFY(focus);
        auto point = focus->mapToScene(QPointF(focus->width() / 2, focus->height() / 2)).toPoint();
        QTest::mousePress(w, Qt::LeftButton, {}, point);
        QVERIFY(machine.firing());
        QTest::mouseRelease(w, Qt::LeftButton, {}, point);
        QVERIFY(!machine.firing());
        QTest::mousePress(w, Qt::LeftButton, {}, point);
        QVERIFY(machine.firing());
        w->setProperty("page", 0);
        QVERIFY(!machine.firing());
        QTest::mouseRelease(w, Qt::LeftButton, {}, point);
        w->setProperty("night", true);
        QTest::qWait(40);
        QVERIFY(!w->grabWindow().isNull());
        project.newProject();
        auto *materialTest = item("materialTestButton");
        QVERIFY(materialTest);
        click(materialTest);
        auto *testDialog = w->findChild<QObject *>("materialTestDialog");
        QVERIFY(testDialog);
        QTRY_VERIFY(testDialog->property("visible").toBool());
        auto *prepare = item("prepareMaterialTestButton");
        QVERIFY(prepare);
        click(prepare);
        QTRY_COMPARE(project.count(), 16);
        project.newProject();
        QImage reliefMap(16, 16, QImage::Format_ARGB32);
        reliefMap.fill(QColor(128, 128, 128));
        const auto reliefPath = storage.filePath("depth.png");
        QVERIFY(reliefMap.save(reliefPath));
        QVERIFY(project.importRelief(QUrl::fromLocalFile(reliefPath)));
        auto *reliefTab = item("workflow3");
        QVERIFY(reliefTab);
        click(reliefTab);
        QTRY_COMPARE(w->property("page").toInt(), 3);
        QVERIFY(item("reliefPanel"));
        auto *invert = item("reliefInvert");
        QVERIFY(invert);
        click(invert);
        QTRY_VERIFY(project.items()[0].relief.inverted);
        auto *gammaInput = item("reliefGamma");
        QVERIFY(gammaInput);
        click(gammaInput);
        gammaInput->setProperty("inputText", "2,0");
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.items()[0].relief.gamma, 2.);
        project.setReliefSetting("depthPerPass", .02);
        QTRY_VERIFY(item("reliefDepthLabel")->property("text").toString().contains("0.320"));
        auto *slice = item("reliefSlice");
        QVERIFY(slice);
        click(slice);
        QTest::mouseClick(
            w, Qt::LeftButton, {},
            slice->mapToScene(QPointF(slice->width() - 2, slice->height() / 2)).toPoint());
        QTRY_COMPARE(item("reliefPreview")->property("slice").toInt(), 16);
        QVERIFY(!w->grabWindow().isNull());
        w->setProperty("page", 0);

        project.newProject();
        project.addShape("rect");
        click(item("workflow4"));
        QTRY_COMPARE(w->property("page").toInt(), 4);
        click(item("toolsOperations"));
        auto *newOperation = item("newOperationName");
        QVERIFY(newOperation);
        click(newOperation);
        QTest::keyClick(w, Qt::Key_A, Qt::ControlModifier);
        for (char c : QByteArray("Fein"))
            QTest::keyClick(w, c);
        click(item("addOperation"));
        QTRY_COMPARE(project.layers().size(), 3);
        auto *assignment = item("operationAssign2");
        QVERIFY(assignment);
        click(assignment);
        QTRY_VERIFY(project.items()[0].extraOperations.contains(2));
        project.newProject();
        QVERIFY(project.importImage(QUrl::fromLocalFile(reliefPath)));
        click(item("toolsImage"));
        auto *mode = item("imageModeChoice");
        QVERIFY(mode);
        click(mode);
        QTest::keyClick(w, Qt::Key_End);
        QTest::keyClick(w, Qt::Key_Return);
        click(item("imageApply"));
        QTRY_COMPARE(project.items()[0].style["imageMode"].toString(), "grayscale");
        click(item("toolsProduction"));
        QVERIFY(cameraRegistration.load(QUrl::fromLocalFile(reliefPath)));
        auto *registerCamera = item("registerCamera");
        QVERIFY(registerCamera);
        click(registerCamera);
        QTRY_VERIFY(cameraRegistration.calibrated());
        QVERIFY(!w->grabWindow().isNull());
        w->setProperty("page", 0);
        QVERIFY(project.job().valid());
        project.addShape("rect");
        QVERIFY(project.dirty());
        // Save prompt: cancelling the picker or a failed write must keep all changes.
        const auto beforeSave = project.projectBytes();
        QVERIFY(QMetaObject::invokeMethod(w, "requestAction", Q_ARG(QVariant, "new")));
        auto *savePicker = w->findChild<QObject *>("saveProjectDialog");
        QVERIFY(savePicker);
        click(item("saveChangesButton"));
        QTRY_VERIFY(savePicker->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(savePicker, "reject"));
        QCOMPARE(project.projectBytes(), beforeSave);
        QCOMPARE(w->property("pendingAction").toString(), QString());
        QVariant result;
        w->setProperty("pendingAction", "new");
        QVERIFY(QMetaObject::invokeMethod(
            w, "finishSave", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QUrl::fromLocalFile(storage.filePath("missing/project.blazer")))));
        QVERIFY(!result.toBool());
        QCOMPARE(project.projectBytes(), beforeSave);
        QVERIFY(project.dirty());
        QCOMPARE(w->property("pendingAction").toString(), QString());
        w->setProperty("pendingAction", "new");
        const auto savedProject = QUrl::fromLocalFile(storage.filePath("saved-before-new.blazer"));
        QVERIFY(QMetaObject::invokeMethod(w, "finishSave", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, savedProject)));
        QVERIFY(result.toBool());
        QCOMPARE(project.count(), 0);
        QVERIFY(!project.dirty());
        Document reopenedProject;
        QVERIFY(reopenedProject.open(savedProject));
        QCOMPARE(reopenedProject.projectBytes(), beforeSave);
        project.addShape("rect");
        machine.focus(true);
        QVERIFY(!w->close());
        QVERIFY(!machine.firing());
        auto *discard = w->findChild<QObject *>("discardDialog");
        QVERIFY(discard);
        QTRY_VERIFY(discard->property("visible").toBool());
        auto *cancelClose = item("cancelCloseButton");
        QVERIFY(cancelClose);
        click(cancelClose);
        QTRY_VERIFY(!discard->property("visible").toBool());
        QVERIFY(w->isVisible());
        QVERIFY(project.dirty());
        QVERIFY(!w->close());
        QTRY_VERIFY(discard->property("visible").toBool());
        auto *discardChanges = item("discardChangesButton");
        QVERIFY(discardChanges);
        click(discardChanges);
        QTRY_VERIFY_WITH_TIMEOUT(!w->isVisible(), 1500);
        QVERIFY(!discard->property("visible").toBool());
        QVERIFY(!machine.busy());
    }
    void recoveryDialogExplicitDecisions_data() {
        QTest::addColumn<bool>("incomingDirty");
        QTest::addColumn<QString>("decision");
        QTest::newRow("clean-keep") << false << QString("keep");
        QTest::newRow("draft-keep") << true << QString("keep");
        QTest::newRow("clean-restore") << false << QString("restore");
        QTest::newRow("draft-restore") << true << QString("restore");
        QTest::newRow("clean-discard") << false << QString("discard");
        QTest::newRow("draft-discard") << true << QString("discard");
    }
    void recoveryDialogExplicitDecisions() {
        QFETCH(bool, incomingDirty);
        QFETCH(QString, decision);
        Document project;
        QTemporaryDir storage;
        QString recoveryPath;
        {
            Recovery old(&project, storage.filePath("recovery"));
            project.addShape("rect");
            QVERIFY(old.save());
            recoveryPath = old.location();
        }
        QFile original(recoveryPath);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();
        project.newProject();
        Recovery recovery(&project, storage.filePath("recovery"));
        if (incomingDirty)
            project.addShape("text");
        const auto incomingBytes=project.projectBytes();
        MaterialLibrary library(storage.filePath("materials.json"));
        UiMachine machine;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        if(decision!="keep")QVERIFY(services.finishOnboarding());
        Registration cameraRegistration;
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("recovery", &recovery);
        context->setContextProperty("appServices", &services);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("materialLibrary", &library);
        context->setContextProperty("cameraRegistration", &cameraRegistration);
        QFile profile(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/blaze-x-m3.json"));
        QVERIFY(profile.open(QIODevice::ReadOnly));
        context->setContextProperty("deviceProfile",
                                    QJsonDocument::fromJson(profile.readAll()).toVariant());
        engine.load(QUrl::fromLocalFile(QStringLiteral(BLAZER_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *dialog = window->findChild<QObject *>("recoveryDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(!window->findChild<QObject *>("onboardingDialog")->property("visible").toBool());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(50);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto clickRecovery = [&](const char *name) {
            auto *button=window->findChild<QQuickItem *>(name);
            if(!button || !button->isVisible() || !button->isEnabled())return false;
            window->requestActivate();
            if(!QTest::qWaitForWindowActive(window))return false;
            for(int attempt=0;attempt<5;++attempt) {
                const auto point=button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint();
                QTest::mouseMove(window,point);
                QTest::qWait(30);
                if(button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint()!=point)continue;
                QTest::mouseClick(window,Qt::LeftButton,{},point);
                return true;
            }
            return false;
        };
        if(decision=="restore") {
            QVERIFY(clickRecovery("restoreRecovery"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
            QVERIFY(!recovery.pendingDecision());
            QCOMPARE(project.projectBytes(),bytes);
            QVERIFY(project.dirty());
            QVERIFY(project.fileUrl().isEmpty());
            QCOMPARE(project.history().last().toMap()["label"].toString(),QString("Wiederherstellung"));
            const auto restoredFile=QUrl::fromLocalFile(storage.filePath("restored.blazer"));
            QVERIFY(project.save(restoredFile));
            QVERIFY(!project.dirty());
            QVERIFY(!recovery.available());
            Document check;
            QVERIFY(check.open(restoredFile));
            QCOMPARE(check.projectBytes(),bytes);
            QVERIFY(window->close());
            QTRY_VERIFY(!window->isVisible());
            QVERIFY(!QFile::exists(recoveryPath));
            return;
        }
        if(decision=="discard") {
            QVERIFY(clickRecovery("discardRecovery"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
            QVERIFY(!recovery.pendingDecision());
            QCOMPARE(project.projectBytes(),incomingBytes);
            QVERIFY(!QFile::exists(recoveryPath));
            if(incomingDirty) {
                QTest::qWait(1800);
                QVERIFY(recovery.available());
                QVERIFY(original.open(QIODevice::ReadOnly));
                QCOMPARE(original.readAll(),incomingBytes);
                original.close();
                QVERIFY(!window->close());
                QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
                QVERIFY(clickRecovery("discardChangesButton"));
            } else QVERIFY(window->close());
            QTRY_VERIFY(!window->isVisible());
            QVERIFY(!QFile::exists(recoveryPath));
            return;
        }
        QTest::keyClick(window, Qt::Key_Escape);
        if (incomingDirty) {
            auto *discard = window->findChild<QObject *>("discardDialog");
            QTRY_VERIFY(discard->property("visible").toBool());
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(dialog->property("visible").toBool());
            QVERIFY(recovery.pendingDecision());
            const auto before = project.projectBytes();
            QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
            QCOMPARE(project.projectBytes(), before);
            QTest::qWait(1800);
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(discard->property("visible").toBool());
            auto *save = window->findChild<QQuickItem *>("saveChangesButton");
            QVERIFY(save);
            QTest::mouseClick(window, Qt::LeftButton, {},
                              save->mapToScene(QPointF(save->width()/2, save->height()/2)).toPoint());
            auto *picker = window->findChild<QObject *>("saveProjectDialog");
            QVERIFY(picker);
            QTRY_VERIFY(picker->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(picker, "reject"));
            QTRY_VERIFY(dialog->property("visible").toBool());
            QTRY_VERIFY(dialog->property("opened").toBool());
            window->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(window));
            QTRY_VERIFY(dialog->property("activeFocus").toBool());
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(discard->property("visible").toBool());
            auto *button = window->findChild<QQuickItem *>("discardChangesButton");
            QVERIFY(button);
            QTest::mouseClick(window, Qt::LeftButton, {},
                              button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        }
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
