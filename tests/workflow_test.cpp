#include "applicationservices.h"
#include "document.h"
#include "imageprocessing.h"
#include "machine.h"
#include "materiallibrary.h"
#include "registration.h"
#include "reliefpreview.h"
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

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name, bool visibleOnly = false) {
    if (root->objectName() == name && (!visibleOnly || root->isVisible()))
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findItem(child, name, visibleOnly))
            return found;
    return nullptr;
}
void typeText(QQuickWindow *window, const QByteArray &text) {
    for (const char character : text)
        QTest::keyClick(window, character);
}
bool click(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled())
        return false;
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window, 1000))
        return false;
    QTest::qWait(50); // Let layout changes after file selection settle before scrolling.
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid())
            continue;
        const auto point = item->mapToItem(parent, QPointF());
        const double delta = point.y() < 5 ? point.y() - 5
                             : point.y() + item->height() > parent->height() - 5
                                 ? point.y() + item->height() - parent->height() + 5
                                 : 0;
        const double limit =
            std::max(0., parent->property("contentHeight").toDouble() - parent->height());
        parent->setProperty("contentY",
                            std::clamp(parent->property("contentY").toDouble() + delta, 0., limit));
    }
    QTest::qWait(30);
    // In the offscreen backend a pointer move may polish a pending layout.
    // Recalculate after that frame instead of clicking its old coordinates.
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        if (!QRect(0, 0, window->width(), window->height()).contains(point))
            return false;
        QTest::mouseMove(window, point);
        QTest::qWait(20);
        if (item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint() == point) {
            QTest::mouseClick(window, Qt::LeftButton, {}, point);
            return true;
        }
    }
    return false;
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class WorkflowTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    }
    void completeApplicationWorkflow_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("light") << false;
        QTest::newRow("compact-dark") << true;
    }
    void completeApplicationWorkflow() {
        QFETCH(bool, compactDark);
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        Document project;
        project.newProject();
        project.setBackgroundPlanning(true);
        Machine machine(&project, nullptr, Machine::HardwareAccess::Disabled);
        MaterialLibrary library(storage.filePath("materials.json"));
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding()); // Onboarding itself has a separate UI test.
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("materialLibrary", &library);
        context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        for (const auto &pair :
             {qMakePair("deviceProfile", "blaze-x-m3.json"),
              qMakePair("manufacturerReference", "manufacturer-materials.json")}) {
            const auto bytes = read(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/") + pair.second);
            QVERIFY(!bytes.isEmpty());
            context->setContextProperty(pair.first, QJsonDocument::fromJson(bytes).toVariant());
        }
        engine.load(QUrl::fromLocalFile(QStringLiteral(BLAZER_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        if (compactDark) {
            window->resize(1180, 760);
            window->setProperty("night", true);
        }
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [window](const QString &name) { return findItem(window->contentItem(), name); };
        auto screenshot = [window](const QString &stage) {
            if (!qEnvironmentVariableIsSet("BLAZER_WORKFLOW_SCREENSHOT"))
                return true;
            QTest::qWait(80);
            return window->grabWindow().save(qEnvironmentVariable("BLAZER_WORKFLOW_SCREENSHOT") +
                                             "." + QTest::currentDataTag() + "." + stage + ".png");
        };
        auto chooseFile = [window](const char *name, const QString &path) {
            auto *dialog = window->findChild<QObject *>(name);
            if (!dialog || !dialog->setProperty("selectedFile", QUrl::fromLocalFile(path)))
                return false;
            // Inject the platform picker's result into the real QML handler.
            // Native desktop picker interaction is a separate desktop test.
            return QMetaObject::invokeMethod(dialog, "accepted") &&
                   QMetaObject::invokeMethod(dialog, "reject");
        };
        auto projectMenuAction = [&](const QString &name) {
            if (!click(window, item("projectDetailsButton")))
                return false;
            if (!QTest::qWaitFor([&] { return item(name) && item(name)->isVisible(); }, 1000))
                return false;
            return click(window, item(name));
        };
        auto openHistory = [&] {
            if (!click(window, item("openHistoryButton")))return false;
            auto *dialog=window->findChild<QObject *>("historyDialog");
            return dialog && QTest::qWaitFor([&]{return dialog->property("opened").toBool();},1000);
        };
        auto chooseHistory = [&](int position) {
            auto *list=item("historyList");
            if(!list)return false;
            list->setProperty("contentY",position==0 ? 0. : std::max(0.,list->property("contentHeight").toDouble()-list->height()));
            const auto name="historyRow"+QString::number(position);
            if(!QTest::qWaitFor([&]{return item(name) && item(name)->isVisible();},1000))return false;
            return click(window,item(name)) && click(window,item("historyApply"));
        };
        project.addShape("rect");
        QVERIFY(item("objectCheck0"));
        QVERIFY(project.addPage("Rückseite"));
        auto *pageIndicator = item("activePageIndicator");
        QVERIFY(pageIndicator);
        QVERIFY(pageIndicator->property("text").toString().contains("Rückseite"));
        QTRY_VERIFY(!item("objectCheck0")->isVisible());
        project.newProject();
        // The target chooser is separate from the actual output sheet.
        // Rename/copy/delete must act on that chosen target, and may not
        // silently redirect output from another sheet.
        QVERIFY(project.renamePage(0, "Vorne"));
        project.addShape("rect");
        project.setProperty("width", 5.);
        project.setProperty("height", 4.);
        project.addShape("ellipse");
        project.toggleSelection(0);
        project.groupSelection();
        QVERIFY(project.addPage("Hinten"));
        project.addShape("text");
        project.setProperty("text", "B");
        project.activatePage(0);
        window->setProperty("basic", false);
        QVERIFY(click(window, item("workflow4")));
        QVERIFY(click(window, item("toolsProduction")));
        auto choosePage = [&](int index) {
            if (!click(window, item("pageTarget")))
                return false;
            auto *popup = item("pageTarget")->property("popup").value<QObject *>();
            if (!popup || !QTest::qWaitFor([&] { return popup->property("opened").toBool(); }, 1000)) {
                qWarning() << "page popup did not open" << index << item("pageTarget")->property("currentIndex")
                           << item("pageTarget")->hasActiveFocus() << (popup ? popup->property("visible") : QVariant());
                screenshot("page-picker-failure");
                return false;
            }
            const auto previous = item("pageTarget")->property("currentIndex").toInt();
            if (!QMetaObject::invokeMethod(&project, "changed") ||
                item("pageTarget")->property("currentIndex").toInt() != previous ||
                !popup->property("opened").toBool())
                return false;
            QTest::keyClick(window, Qt::Key_Home);
            for (int i = 0; i < index; ++i)
                QTest::keyClick(window, Qt::Key_Down);
            QTest::keyClick(window, Qt::Key_Return);
            if (!QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 1000)) {
                qWarning() << "page popup did not close" << index;
                screenshot("page-picker-failure");
                return false;
            }
            return item("pageTarget")->property("currentIndex").toInt() == index;
        };
        auto pageName = [&](const QByteArray &name) {
            if (!click(window, item("pageNameEditor")))
                return false;
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            typeText(window, name);
            return true;
        };
        QVERIFY(choosePage(1));
        // Planning progress uses Document::changed too. It must not reset
        // a deliberately chosen target to the current output sheet.
        QVERIFY(QMetaObject::invokeMethod(&project, "changed"));
        QCOMPARE(item("pageTarget")->property("currentIndex").toInt(), 1);
        QCOMPARE(project.activePage(), 0);
        QVERIFY(pageIndicator->property("text").toString().contains("Vorne"));
        const auto beforeNameDraft = project.projectBytes();
        QVERIFY(pageName("Name draft"));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(project.projectBytes(), beforeNameDraft);
        QVERIFY(pageName("Rueckseite"));
        QVERIFY(click(window, item("renamePageButton")));
        QCOMPARE(project.pages(), QStringList({"Vorne", "Rueckseite"}));
        QCOMPARE(project.activePage(), 0);
        Document target;
        QVERIFY(target.restoreBytes(project.projectBytes()));
        target.activatePage(1);
        const auto targetGcode = target.job().gcode();
        QVERIFY(target.job().valid());
        QVERIFY(choosePage(1));
        QVERIFY(pageName("Kopie"));
        QVERIFY(click(window, item("copyPageButton")));
        QCOMPARE(project.pages(), QStringList({"Vorne", "Rueckseite", "Kopie"}));
        QCOMPARE(project.activePage(), 2);
        QCOMPARE(project.count(), 4);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        QCOMPARE(project.job().gcode(), targetGcode);
        QVERIFY(pageIndicator->property("text").toString().contains("Kopie"));
        QVERIFY(item("activeOutputPage")->property("text").toString().contains("Kopie"));
        QVERIFY(screenshot("sheets"));
        QVERIFY(project.setMetadata(QString(1000, 'P'), ""));
        QVERIFY(project.renamePage(2, "<b>" + QString(193, 'X') + "</b>"));
        QTest::qWait(100);
        const auto startBounds = item("startJobButton")->mapRectToScene(
            QRectF(0, 0, item("startJobButton")->width(), item("startJobButton")->height()));
        QVERIFY2(startBounds.right() <= window->width(), "long names hide the start button");
        auto *footer = item("activePageFooter"), *hint = item("workspaceHint");
        QVERIFY(footer && hint);
        const auto footerBounds = footer->mapRectToScene(QRectF(0, 0, footer->width(), footer->height()));
        const auto hintBounds = hint->mapRectToScene(QRectF(0, 0, hint->width(), hint->height()));
        QVERIFY2(footerBounds.right() <= hintBounds.left() + 1, "long sheet name overlaps hints");
        const auto parentBounds = hint->parentItem()->mapRectToScene(
            QRectF(0, 0, hint->parentItem()->width(), hint->parentItem()->height()));
        QVERIFY2(hintBounds.right() <= parentBounds.right() + 1, "footer overflows its workspace");
        QVERIFY2(item("activeOutputPage")->property("paintedWidth").toDouble() <=
                     item("activeOutputPage")->width() + 1,
                 "long unbroken sheet name overflows its label");
        QVERIFY(screenshot("sheets-long-names"));
        const auto headerPoint = pageIndicator->mapToScene(
            QPointF(pageIndicator->width() / 2, pageIndicator->height() / 2));
        QTest::mouseMove(window, headerPoint.toPoint());
        auto *pageTip = window->findChild<QObject *>("activePageTooltip");
        QVERIFY(pageTip);
        QTRY_VERIFY(pageTip->property("opened").toBool());
        auto *tipContent = pageTip->property("contentItem").value<QQuickItem *>();
        QVERIFY(tipContent);
        QVERIFY(tipContent->property("paintedWidth").toDouble() <= tipContent->width() + 1);
        QVERIFY(screenshot("sheets-long-tooltip"));
        QVERIFY(project.setMetadata("Unbenannt", ""));
        QVERIFY(project.renamePage(2, "Kopie"));
        QVERIFY(choosePage(0));
        QCOMPARE(project.activePage(), 2);
        QVERIFY(click(window, item("activatePageButton")));
        QCOMPARE(project.activePage(), 0);
        project.selectGroup(0);
        QVERIFY(choosePage(1));
        QVERIFY(click(window, item("copySelectionPageButton")));
        QCOMPARE(project.count(), 6);
        QCOMPARE(project.activePage(), 0);
        QVERIFY(!project.items()[4].groupId.isEmpty());
        QCOMPARE(project.items()[4].groupId, project.items()[5].groupId);
        QVERIFY(project.items()[4].groupId != project.items()[0].groupId);
        QVERIFY(choosePage(1));
        QVERIFY(click(window, item("removePageButton")));
        QCOMPARE(project.pages(), QStringList({"Vorne", "Kopie"}));
        QCOMPARE(project.count(), 3);
        QCOMPARE(project.activePage(), 0);
        QVERIFY(pageIndicator->property("text").toString().contains("Vorne"));
        QVERIFY(click(window, item("workflow0")));
        QVERIFY(click(window, item("workspace")));
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(project.pages().size(), 3);
        QCOMPARE(project.count(), 6);
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(project.pages().size(), 2);
        project.newProject();
        window->findChild<QObject *>("toolsPanel")->setProperty("section", 0);
        window->setProperty("basic", true);
        // Project metadata is a modal draft. Editor shortcuts and Escape must
        // not change the underlying selection, history or project files.
        project.addShape("rect");
        const auto beforeDetails = project.projectBytes();
        const int beforeSelection = project.selected();
        auto *details = window->findChild<QObject *>("projectDetailsDialog");
        QVERIFY(details);
        QVERIFY(projectMenuAction("projectMetadataMenuItem"));
        QTRY_VERIFY(details->property("visible").toBool());
        QVERIFY(click(window, item("projectNotesEditor")));
        typeText(window, "Discarded notes");
        QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(project.projectBytes(), beforeDetails);
        QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier);
        QVERIFY(!window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        QVERIFY(!window->findChild<QObject *>("openProjectDialog")->property("visible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!details->property("visible").toBool());
        QCOMPARE(project.projectBytes(), beforeDetails);
        QCOMPARE(project.selected(), beforeSelection);
        QVERIFY(projectMenuAction("projectMetadataMenuItem"));
        QTRY_VERIFY(details->property("visible").toBool());
        QVERIFY(item("projectNotesEditor")->property("text").toString().isEmpty());
        item("projectNotesEditor")->setProperty("text", QString(16001, 'x'));
        QTRY_VERIFY(!item("applyProjectDetails")->isEnabled());
        QVERIFY(click(window, item("cancelProjectDetails")));
        QCOMPARE(project.projectBytes(), beforeDetails);
        project.newProject();
        QVERIFY(projectMenuAction("projectMetadataMenuItem"));
        QTRY_VERIFY(details->property("visible").toBool());
        QVERIFY(click(window, item("projectNameEditor")));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        typeText(window, "Rasterversuch");
        QVERIFY(click(window, item("projectNotesEditor")));
        typeText(window, "Kraftpapier 80 x 80 mm.");
        QTest::keyClick(window, Qt::Key_Return);
        typeText(window, "<b>Oberflaeche pruefen.</b>");
        const QString expectedNotes = "Kraftpapier 80 x 80 mm.\n<b>Oberflaeche pruefen.</b>";
        QCOMPARE(item("projectNotesEditor")->property("text").toString(), expectedNotes);
        QVERIFY(screenshot("project"));
        QVERIFY(click(window, item("applyProjectDetails")));
        QTRY_VERIFY(!details->property("visible").toBool());
        QCOMPARE(project.title(), QString("Rasterversuch"));
        QCOMPARE(project.notes(), expectedNotes);
        QVERIFY(project.dirty());
        const int sourceRow = library.indexOf("vendor-m3pro-5w-engrave-1");
        QVERIFY(sourceRow >= 0);
        const auto sourceProfile = library.profileAt(sourceRow);
        const auto filtered = library.rowsForModule(5);
        const int materialRow = filtered.indexOf(sourceRow);
        QVERIFY(materialRow >= 0);
        QVERIFY(click(window, item("materialChoice")));
        QTest::keyClick(window, Qt::Key_Home);
        for (int i = 0; i < materialRow; ++i)
            QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project.material()["id"].toString(), sourceProfile["id"].toString());
        QTRY_VERIFY(item("basicValues")
                        ->property("text")
                        .toString()
                        .contains("Geschwindigkeit: 5000 mm/min"));
        QVERIFY(item("basicValues")->property("text").toString().contains("Leistung: 100 %"));
        QVERIFY(click(window, item("manufacturerButton")));
        auto *reference = window->findChild<QObject *>("manufacturerDialog");
        QVERIFY(reference);
        QTRY_VERIFY(reference->property("visible").toBool());
        QVERIFY(screenshot("manufacturer"));
        QVERIFY(QMetaObject::invokeMethod(reference, "close"));

        const auto svgPath = storage.filePath("motif.svg");
        QFile svg(svgPath);
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"6mm\" height=\"4mm\" "
                  "viewBox=\"0 0 6 4\"><rect width=\"6\" height=\"4\"/></svg>");
        svg.close();
        QImage image(16, 16, QImage::Format_ARGB32);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                image.setPixel(x, y, qRgb(x * 17, x * 17, x * 17));
        const auto imagePath = storage.filePath("gradient.png");
        QVERIFY(image.save(imagePath));
        for (const auto &file : {svgPath, imagePath}) {
            QVERIFY(click(window, item("importMotifButton")));
            QTRY_VERIFY(
                window->findChild<QObject *>("importMotifDialog")->property("visible").toBool());
            QVERIFY(chooseFile("importMotifDialog", file));
        }
        QTRY_COMPARE(project.count(), 2);
        QCOMPARE(project.items()[0].bounds.size(), QSizeF(6, 4));
        QCOMPARE(project.items()[1].reliefAsset->image.size(), QSize(16, 16));
        QVERIFY(!project.materialModified());
        QCOMPARE(project.layers()[0].toMap()["power"].toDouble(), 100.);
        QCOMPARE(project.layers()[0].toMap()["speed"].toDouble(), 5000.);
        for (const auto &art : project.items())
            QCOMPARE(art.layer, 0);
        // A cut-only profile cannot be applied to the imported photo. The
        // rejected choice must not remain visually selected in the combobox.
        const auto beforeRejectedProfile = project.projectBytes();
        const int cutRow = filtered.indexOf(library.indexOf("vendor-copy-pro5w-plywood-3"));
        QVERIFY(cutRow >= 0);
        QVERIFY(click(window, item("materialChoice")));
        QTest::keyClick(window, Qt::Key_Home);
        for (int i = 0; i < cutRow; ++i)
            QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project.material()["id"].toString(), sourceProfile["id"].toString());
        QTRY_COMPARE(item("materialChoice")->property("currentIndex").toInt(), materialRow);
        QCOMPARE(project.projectBytes(), beforeRejectedProfile);

        auto *xField = item("objectXInput");
        QVERIFY(xField);
        auto *numeric = xField->findChild<QQuickItem *>("numericEditor");
        QVERIFY(click(window, numeric));
        QVERIFY(screenshot("editor"));
        QVERIFY(numeric->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_2);
        QTest::keyClick(window, Qt::Key_5);
        QTest::keyClick(window, Qt::Key_Comma);
        QTest::keyClick(window, Qt::Key_0);
        QCOMPARE(xField->property("inputText").toString(), QString("25,0"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project.selection()["x"].toDouble(), 25.);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        const auto originalGcode = project.job().gcode();
        const auto originalEnd = project.job().moves.last().to;
        QVERIFY(originalGcode.join('\n').contains("S1000"));
        QVERIFY(originalGcode.contains("M5 S0"));

        const auto projectPath = storage.filePath("saved.blazer");
        QVERIFY(click(window, item("saveProjectButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        QVERIFY(chooseFile("saveProjectDialog", projectPath));
        QTRY_VERIFY(!project.dirty());
        const auto originalBytes = project.projectBytes();
        QVERIFY(!read(projectPath).isEmpty());
        window->setProperty("basic", false);
        QVERIFY(click(window, item("exportGcodeButton")));
        const auto gcodePath = storage.filePath("saved.gcode");
        QTRY_VERIFY(
            window->findChild<QObject *>("exportGcodeDialog")->property("visible").toBool());
        QVERIFY(chooseFile("exportGcodeDialog", gcodePath));
        QCOMPARE(read(gcodePath), (originalGcode.join('\n') + '\n').toUtf8());
        QVERIFY(click(window,item("workflow4")));
        QVERIFY(click(window,item("toolsImage")));
        QVERIFY(click(window,item("exportProcessedImageButton")));
        auto *imagePicker=window->findChild<QObject *>("imageExportDialog");
        QVERIFY(imagePicker);
        QTRY_VERIFY(imagePicker->property("visible").toBool());
        QCOMPARE(imagePicker->property("processed").toBool(),true);
        const auto processedPath=storage.filePath("processed.png");
        QVERIFY(chooseFile("imageExportDialog",processedPath));
        QTRY_VERIFY_WITH_TIMEOUT(!project.imageExportBusy(),5000);
        QString imageError;
        const auto expectedBitmap=ImageProcessing::apply(project.items()[1].reliefAsset->image,project.items()[1].style,imageError);
        QVERIFY(imageError.isEmpty());
        QCOMPARE(QImage(processedPath).convertToFormat(QImage::Format_ARGB32),expectedBitmap);
        QCOMPARE(project.projectBytes(),originalBytes);
        QVERIFY(!project.dirty());
        QVERIFY(screenshot("processed-image-export"));
        QVERIFY(click(window,item("exportSourceImageButton")));
        QTRY_VERIFY(imagePicker->property("visible").toBool());
        QCOMPARE(imagePicker->property("processed").toBool(),false);
        const auto sourceExportPath=storage.filePath("source.bmp");
        QVERIFY(chooseFile("imageExportDialog",sourceExportPath));
        QTRY_VERIFY_WITH_TIMEOUT(!project.imageExportBusy(),5000);
        QCOMPARE(QImage(sourceExportPath).convertToFormat(QImage::Format_ARGB32),project.items()[1].reliefAsset->image.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(project.job().gcode(),originalGcode);
        QVERIFY(!machine.busy());
        window->findChild<QObject *>("toolsPanel")->setProperty("section",0);
        QVERIFY(click(window,item("workflow0")));
        window->setProperty("basic", true);
        QVERIFY(click(window, item("startJobButton"))); // Preview, no machine job yet.
        QTRY_COMPARE(window->property("page").toInt(), 1);
        QVERIFY(!machine.busy());
        QVERIFY(!window->grabWindow().isNull());
        QVERIFY(screenshot("preview"));
        QVERIFY(click(window, item("workflow2")));
        QVERIFY(click(window, item("frameJobButton")));
        QTRY_VERIFY(machine.busy());
        QVERIFY(!machine.firing());
        QTRY_VERIFY_WITH_TIMEOUT(machine.ready(), 6000);
        QVERIFY(screenshot("device"));
        QVERIFY(click(window, item("startJobButton")));
        QTRY_VERIFY(machine.running());
        QVERIFY(machine.simulation());
        QVERIFY(machine.log().join('\n').contains("Berechnete Laserpfade simulieren"));
        QVERIFY(!machine.log().join('\n').contains("Demoablauf gestartet"));
        QVERIFY(click(window, item("pauseJobButton")));
        QTRY_VERIFY(machine.paused());
        const double stopped = machine.progress();
        QTest::qWait(80);
        QCOMPARE(machine.progress(), stopped);

        // Editing the next design must not replace the running frozen plan.
        project.select(0);
        project.toggleSelection(1);
        const double oldX = project.selection()["x"].toDouble();
        project.setProperty("x", oldX + 20);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        QCOMPARE(project.job().moves.last().to.x(), originalEnd.x() + 20);
        QVERIFY(project.copyPage(0, "Naechster Durchlauf"));
        QCOMPARE(project.activePage(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        QCOMPARE(project.job().moves.last().to.x(), originalEnd.x() + 20);
        const int pausedHistoryPosition=project.historyPosition();
        const auto pausedDocument=project.projectBytes();
        QVERIFY(openHistory());
        QVERIFY(chooseHistory(0));
        QCOMPARE(project.count(),0);
        QVERIFY(machine.paused());
        QVERIFY(click(window,item("historyClose")));
        QTRY_VERIFY(!window->findChild<QObject *>("historyDialog")->property("visible").toBool());
        QVERIFY(openHistory());
        QVERIFY(chooseHistory(pausedHistoryPosition));
        QCOMPARE(project.projectBytes(),pausedDocument);
        QVERIFY(machine.paused());
        QVERIFY(click(window,item("historyClose")));
        QTRY_VERIFY(!window->findChild<QObject *>("historyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("pauseJobButton")));
        QTRY_VERIFY_WITH_TIMEOUT(machine.ready(), 18000);
        QCOMPARE(machine.progress(), 1.);
        QCOMPARE(machine.x(), originalEnd.x());
        QCOMPARE(machine.y(), originalEnd.y());
        QVERIFY(!machine.firing());
        QVERIFY(project.dirty());

        QVERIFY(click(window, item("openProjectButton")));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("discardChangesButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("openProjectDialog")->property("visible").toBool());
        QVERIFY(chooseFile("openProjectDialog", projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        QCOMPARE(project.projectBytes(), originalBytes);
        QCOMPARE(project.fileUrl(), QUrl::fromLocalFile(projectPath));
        QCOMPARE(project.title(), QString("Rasterversuch"));
        QCOMPARE(project.notes(), expectedNotes);
        QCOMPARE(project.job().gcode(), originalGcode);
        QCOMPARE(library.profileAt(sourceRow), sourceProfile);
        QCOMPARE(services.recentProjects().size(), 1);
        QCOMPARE(services.recentProjects().first().toMap()["url"].toUrl(),
                 QUrl::fromLocalFile(projectPath));
        QVERIFY(project.setMetadata("Other project", "Other project notes"));
        const auto otherPath = storage.filePath("other.blazer");
        QVERIFY(project.save(QUrl::fromLocalFile(otherPath)));
        auto pickRecent = [&](const QString &path) {
            int row = -1;
            const auto recent = services.recentProjects();
            for (int i = 0; i < recent.size(); ++i)
                if (recent[i].toMap()["url"].toUrl() == QUrl::fromLocalFile(path))
                    row = i;
            if (row < 0 || !click(window, item("recentProjectsButton")))
                return false;
            const QString name = "recentProject" + QString::number(row);
            if (!QTest::qWaitFor([&] { return item(name) != nullptr; }, 1000))
                return false;
            return click(window, item(name));
        };
        QVERIFY(project.setMetadata("Unsaved edit", "Save this separately"));
        const auto unsaved = project.projectBytes();
        QVERIFY(pickRecent(projectPath));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("cancelCloseButton")));
        QCOMPARE(project.projectBytes(), unsaved);
        QVERIFY(window->property("pendingProjectFile").toUrl().isEmpty());
        QVERIFY(pickRecent(projectPath));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("saveChangesButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        const auto thirdPath = storage.filePath("saved-before-switch.blazer");
        // Saving reorders the history. The pending file must remain the URL
        // chosen earlier, rather than following the row that now moved.
        QVERIFY(chooseFile("saveProjectDialog", thirdPath));
        QTRY_COMPARE(project.projectBytes(), originalBytes);
        QCOMPARE(read(thirdPath), unsaved);
        QCOMPARE(services.recentProjects().size(), 3);
        QCOMPARE(services.recentProjects().first().toMap()["url"].toUrl(),
                 QUrl::fromLocalFile(projectPath));
        QVERIFY(project.setMetadata("Discard this", "Unneeded changes"));
        QVERIFY(pickRecent(otherPath));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("discardChangesButton")));
        QTRY_COMPARE(project.title(), QString("Other project"));
        QCOMPARE(project.notes(), QString("Other project notes"));
        QVERIFY(!project.dirty());
        const auto current = project.projectBytes();
        QCOMPARE(project.fileUrl(), QUrl::fromLocalFile(otherPath));
        const auto currentSelection = project.selected();
        const auto currentLog = machine.log();
        QVERIFY(QFile::remove(thirdPath));
        QVERIFY(click(window, item("recentProjectsButton")));
        QTRY_VERIFY(item("recentProject2"));
        QTRY_VERIFY(!item("recentProject2")->isEnabled());
        QVERIFY(screenshot("recent"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(
            !window->findChild<QObject *>("recentProjectsDialog")->property("visible").toBool());
        QCOMPARE(project.selected(), currentSelection);
        QCOMPARE(project.projectBytes(), current);
        QCOMPARE(machine.log(), currentLog);
        QVERIFY(click(window, item("recentProjectsButton")));
        QVERIFY(click(window, item("clearRecentProjects")));
        QTRY_VERIFY(services.recentProjects().isEmpty());
        QVERIFY(QFile::exists(projectPath));
        QVERIFY(QFile::exists(otherPath));
        QCOMPARE(project.projectBytes(), current);
        QVERIFY(click(window, item("closeRecentProjects")));
        QVERIFY(project.setMetadata("Template base", "Reusable project notes"));
        const auto templateContent = project.projectBytes();
        QVERIFY(click(window, item("projectDetailsButton")));
        QVERIFY(screenshot("template-menu"));
        QVERIFY(click(window, item("saveTemplateMenuItem")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveTemplateDialog")->property("visible").toBool());
        const auto templatePath = storage.filePath("reusable.blazertemplate");
        QVERIFY(chooseFile("saveTemplateDialog", templatePath));
        QVERIFY(project.dirty());
        QCOMPARE(project.projectBytes(), templateContent);
        QVERIFY(services.recentProjects().isEmpty());
        const auto templateFileBytes = read(templatePath);
        QVERIFY(!templateFileBytes.isEmpty());
        QVERIFY(projectMenuAction("newTemplateMenuItem"));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("cancelCloseButton")));
        QCOMPARE(project.projectBytes(), templateContent);
        QVERIFY(!window->findChild<QObject *>("openTemplateDialog")->property("visible").toBool());
        QVERIFY(projectMenuAction("newTemplateMenuItem"));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("discardChangesButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("openTemplateDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>("openTemplateDialog"),
                                          "reject"));
        QCOMPARE(project.projectBytes(), templateContent);
        // Ordinary saving must neither replace the source template nor start
        // the queued template action after that save has been rejected.
        QVERIFY(projectMenuAction("newTemplateMenuItem"));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("saveChangesButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        QVERIFY(chooseFile("saveProjectDialog", templatePath));
        QVERIFY(!window->findChild<QObject *>("openTemplateDialog")->property("visible").toBool());
        QCOMPARE(project.projectBytes(), templateContent);
        QCOMPARE(read(templatePath), templateFileBytes);
        QVERIFY(project.dirty());
        QVERIFY(projectMenuAction("newTemplateMenuItem"));
        QTRY_VERIFY(window->findChild<QObject *>("discardDialog")->property("visible").toBool());
        QVERIFY(click(window, item("saveChangesButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        const auto beforeTemplatePath = storage.filePath("saved-before-template.blazer");
        QVERIFY(chooseFile("saveProjectDialog", beforeTemplatePath));
        QTRY_VERIFY(
            window->findChild<QObject *>("openTemplateDialog")->property("visible").toBool());
        window->setProperty("proofConfirmed", true);
        window->setProperty("probeKey", window->property("parameterKey"));
        QVERIFY(chooseFile("openTemplateDialog", templatePath));
        QTRY_COMPARE(project.projectBytes(), templateContent);
        QTRY_VERIFY_WITH_TIMEOUT(!project.planningBusy() && project.job().valid(), 5000);
        QVERIFY(project.dirty());
        QVERIFY(!project.canUndo());
        QVERIFY(!project.canRedo());
        QVERIFY(project.fileUrl().isEmpty());
        QVERIFY(!window->property("proofConfirmed").toBool());
        QVERIFY(window->property("probeKey").toString().isEmpty());
        QCOMPARE(window->property("page").toInt(), 0);
        QCOMPARE(services.recentProjects().size(), 1);
        QCOMPARE(read(templatePath), templateFileBytes);
        QVERIFY(project.setMetadata("Independent project", "Notes for this instance"));
        QVERIFY(click(window, item("saveProjectButton")));
        QTRY_VERIFY(
            window->findChild<QObject *>("saveProjectDialog")->property("visible").toBool());
        auto fileNameEditor = []() -> QQuickItem * {
            for (auto *candidate : QGuiApplication::allWindows())
                if (auto *quick = qobject_cast<QQuickWindow *>(candidate);
                    quick && quick->isVisible())
                    if (auto *editor = findItem(quick->contentItem(), "fileNameTextField", true))
                        return editor;
            return nullptr;
        };
        QTRY_VERIFY(fileNameEditor());
        // Qt's selectedFile getter appends defaultSuffix even to an empty URL.
        // Verify the filename the user actually sees, not that derived getter.
        QTRY_COMPARE(fileNameEditor()->property("text").toString(), QString());
        const auto independentPath = storage.filePath("independent.blazer");
        QVERIFY(chooseFile("saveProjectDialog", independentPath));
        QVERIFY(!project.dirty());
        QVERIFY(QFile::exists(independentPath));
        QCOMPARE(read(templatePath), templateFileBytes);
        QCOMPARE(services.recentProjects().size(), 2);
        auto saveResourceCount = [&] {
            int count = 0;
            for (auto *child : window->children())
                if (child->property("title").toString() == "Blazer-Projekt speichern" &&
                    child->property("selectedFile").isValid() &&
                    child->property("currentFolder").isValid())
                    ++count;
            return count;
        };
        // Multiple save operations may retain only the current wrapper and
        // its fallback chooser, rather than one hidden chooser per operation.
        QTRY_VERIFY(saveResourceCount() <= 2);
        // The history is a deliberate modal list, with a saved-content marker,
        // local undo/redo keys and stale-target rejection.
        project.newProject();
        project.addShape("rect");
        QVERIFY(project.setMetadata("Verlauf","Private Notizen"));
        QVERIFY(project.addPage("Rueckseite"));
        project.addShape("ellipse");
        const auto historyFile=storage.filePath("history-ui.blazer");
        QVERIFY(project.save(QUrl::fromLocalFile(historyFile)));
        const auto fileBytes=read(historyFile);
        QVERIFY(project.setMetadata("Geaendert","Neue private Notizen"));
        QVERIFY(openHistory());
        auto *historyDialog=window->findChild<QObject *>("historyDialog");
        QVERIFY(historyDialog);
        QCOMPARE(historyDialog->property("currentPosition").toInt(),5);
        const auto entries=historyDialog->property("entries").toList();
        QVERIFY(entries[4].toMap()["saved"].toBool());
        QVERIFY(chooseHistory(1));
        QCOMPARE(project.count(),1);
        QCOMPARE(project.pages().size(),1);
        QCOMPARE(project.title(),QString("Unbenannt"));
        QCOMPARE(read(historyFile),fileBytes);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(project.historyPosition(),0);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);
        QTRY_COMPARE(project.historyPosition(),1);
        QVERIFY(screenshot("history"));
        auto *historyList=item("historyList");
        QVERIFY(historyList);
        historyList->setProperty("contentY",0.);
        QTRY_VERIFY(item("historyRow4") && item("historyRow4")->isVisible());
        QVERIFY(click(window,item("historyRow4")));
        const int chosen=historyDialog->property("selectedPosition").toInt();
        QVERIFY(QMetaObject::invokeMethod(&project,"changed"));
        QCOMPARE(historyDialog->property("selectedPosition").toInt(),chosen);
        QVERIFY(project.setMetadata("Extern geaendert",""));
        const auto staleBytes=project.projectBytes();
        QVERIFY(click(window,item("historyApply")));
        QVERIFY(item("historyStaleMessage")->isVisible());
        QCOMPARE(project.projectBytes(),staleBytes);
        QCOMPARE(read(historyFile),fileBytes);
        const int selected=project.selected();
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY(!historyDialog->property("visible").toBool());
        QCOMPARE(project.projectBytes(),staleBytes);
        QCOMPARE(project.selected(),selected);
        QVERIFY(projectMenuAction("historyMenuItem"));
        QTRY_VERIFY(historyDialog->property("visible").toBool());
        QVERIFY(click(window,item("historyClose")));
        QTRY_VERIFY(!historyDialog->property("visible").toBool());
        project.newProject();
        // The actual picker handler imports GCode geometry with the selected
        // Basic profile, without starting a machine job or copying source power.
        window->setProperty("basic",true);
        QVERIFY(project.applyBasicMaterial(sourceProfile));
        const auto sourceGcodePath=storage.filePath("geometry.gcode");
        QFile sourceGcode(sourceGcodePath);
        QVERIFY(sourceGcode.open(QIODevice::WriteOnly));
        sourceGcode.write("G21G90\nG0X10Y20\nM4S900\nG1X20F3333\nG1Y25\nM5\nG1X30\nM2\n");
        sourceGcode.close();
        QVERIFY(click(window,item("importMotifButton")));
        auto *importPicker=window->findChild<QObject *>("importMotifDialog");
        QVERIFY(importPicker);
        QTRY_VERIFY(importPicker->property("visible").toBool());
        QVERIFY(importPicker->property("nameFilters").toStringList().join(' ').contains("*.gcode"));
        QVERIFY(chooseFile("importMotifDialog",sourceGcodePath));
        QTRY_COMPARE(project.count(),1);
        QCOMPARE(project.items()[0].bounds,QRectF(0,0,10,5));
        QCOMPARE(project.items()[0].layer,0);
        QCOMPARE(project.layers()[0].toMap()["power"].toDouble(),100.);
        QCOMPARE(project.layers()[0].toMap()["speed"].toDouble(),5000.);
        QVERIFY(!project.materialModified());
        QVERIFY(project.message().contains("GCode"));
        QVERIFY(!machine.running());
        QVERIFY(!machine.paused());
        QVERIFY(screenshot("gcode-import"));
        project.newProject();
        QVERIFY(project.applyBasicMaterial(sourceProfile));
        const auto magePath=storage.filePath("geometry.mage");
        QFile mage(magePath);
        QVERIFY(mage.open(QIODevice::WriteOnly));
        mage.write("<MillMageProject FormatVersion=\"1\"><Shape Type=\"Rect\" W=\"10\" H=\"5\">"
                   "<XForm>1 0 0 1 20 20</XForm></Shape>"
                   "<Operation><Feed Value=\"33333\"/><Spindle Value=\"25000\"/></Operation></MillMageProject>");
        mage.close();
        QVERIFY(click(window,item("importMotifButton")));
        QTRY_VERIFY(importPicker->property("visible").toBool());
        QVERIFY(importPicker->property("nameFilters").toStringList().join(' ').contains("*.mage"));
        QVERIFY(chooseFile("importMotifDialog",magePath));
        QTRY_COMPARE(project.count(),1);
        QCOMPARE(project.items()[0].bounds,QRectF(15,17.5,10,5));
        QCOMPARE(project.material()["id"].toString(),sourceProfile["id"].toString());
        QCOMPARE(project.layers()[0].toMap()["power"].toDouble(),100.);
        QCOMPARE(project.layers()[0].toMap()["speed"].toDouble(),5000.);
        QVERIFY(project.message().contains("MillMage"));
        QVERIFY(!machine.running());
        QVERIFY(screenshot("millmage-import"));
        project.newProject();
        QVERIFY(project.applyBasicMaterial(sourceProfile));
        QVERIFY(click(window,item("importMotifButton")));
        QTRY_VERIFY(importPicker->property("visible").toBool());
        QVERIFY(importPicker->property("nameFilters").toStringList().join(' ').contains("*.rd"));
        QVERIFY(chooseFile("importMotifDialog",QStringLiteral(BLAZER_SOURCE_DIR "/tests/fixtures/ruida/rectangle.rd")));
        QTRY_COMPARE(project.count(),1);
        QCOMPARE(project.items()[0].bounds,QRectF(0,0,20,10));
        QCOMPARE(project.layers()[0].toMap()["power"].toDouble(),100.);
        QCOMPARE(project.layers()[0].toMap()["speed"].toDouble(),5000.);
        QVERIFY(project.message().contains("RDWorks"));
        QVERIFY(!machine.running());
        QVERIFY(screenshot("rdworks-import"));
        project.newProject();
        QVERIFY(machine.simulation());
        QVERIFY(!machine.log().join('\n').contains("Baud geöffnet"));
        QVERIFY(window->close());
    }
};
QTEST_MAIN(WorkflowTest)
#include "workflow_test.moc"
