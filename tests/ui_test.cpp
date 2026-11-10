#include "document.h"
#include "materiallibrary.h"
#include "reliefpreview.h"
#include "simulator.h"
#include "workspace.h"
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
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
    void editNavigateAndFocus() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
        Document project;
        QTemporaryDir storage;
        MaterialLibrary library(storage.filePath("materials.json"));
        UiMachine machine;
        Registration cameraRegistration;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("project", &project);
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
        QTest::keyClick(w, Qt::Key_End);
        QTest::keyClick(w, Qt::Key_Return);
        QTRY_COMPARE(project.items()[3].layer, 1);
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
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
