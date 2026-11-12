#include "applicationservices.h"
#include "document.h"
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
QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findItem(child, name))
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
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    if (!QRectF(0, 0, window->width(), window->height()).contains(point))
        return false;
    QTest::mouseClick(window, Qt::LeftButton, {}, point.toPoint());
    return true;
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
        // Project metadata is a modal draft. Editor shortcuts and Escape must
        // not change the underlying selection, history or project files.
        project.addShape("rect");
        const auto beforeDetails = project.projectBytes();
        const int beforeSelection = project.selected();
        auto *details = window->findChild<QObject *>("projectDetailsDialog");
        QVERIFY(details);
        QVERIFY(click(window, item("projectDetailsButton")));
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
        QVERIFY(click(window, item("projectDetailsButton")));
        QTRY_VERIFY(details->property("visible").toBool());
        QVERIFY(item("projectNotesEditor")->property("text").toString().isEmpty());
        item("projectNotesEditor")->setProperty("text", QString(16001, 'x'));
        QTRY_VERIFY(!item("applyProjectDetails")->isEnabled());
        QVERIFY(click(window, item("cancelProjectDetails")));
        QCOMPARE(project.projectBytes(), beforeDetails);
        project.newProject();
        QVERIFY(click(window, item("projectDetailsButton")));
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
        QCOMPARE(project.title(), QString("Rasterversuch"));
        QCOMPARE(project.notes(), expectedNotes);
        QCOMPARE(project.job().gcode(), originalGcode);
        QCOMPARE(library.profileAt(sourceRow), sourceProfile);
        QVERIFY(machine.simulation());
        QVERIFY(!machine.log().join('\n').contains("Baud geöffnet"));
        QVERIFY(window->close());
    }
};
QTEST_MAIN(WorkflowTest)
#include "workflow_test.moc"
