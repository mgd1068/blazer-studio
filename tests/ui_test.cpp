#include "document.h"
#include "materiallibrary.h"
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

class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void editNavigateAndFocus() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        Document project;
        QTemporaryDir storage;
        MaterialLibrary library(storage.filePath("materials.json"));
        Simulator machine;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("project", &project);
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
