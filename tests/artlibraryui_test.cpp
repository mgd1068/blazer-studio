#include "applicationservices.h"
#include "artlibrary.h"
#include "artlibrarycontroller.h"
#include "devicemanager.h"
#include "document.h"
#include "geometryassetcodec.h"
#include "machine.h"
#include "materiallibrary.h"
#include "materialtestledger.h"
#include "registration.h"
#include "reliefpreview.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
namespace {
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = find(child, name))
            return found;
    return nullptr;
}
bool reveal(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible())
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
    const auto p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    if (!QRect(0, 0, window->width(), window->height()).contains(p))
        return false;
    return true;
}
bool click(QQuickWindow *window, QQuickItem *item, bool requireEnabled = true) {
    if (!item || (requireEnabled && !item->isEnabled()) || !reveal(window, item))
        return false;
    const auto p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, {}, p);
    return true;
}
bool type(QQuickWindow *window, QQuickItem *item, const QString &text) {
    if (!click(window, item))
        return false;
    item->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_Backspace);
    for (const char key : text.toLatin1())
        QTest::keyClick(window, key);
    return true;
}
QRectF allBounds(const Document &document) {
    QRectF bounds;
    bool first = true;
    for (const auto &art : document.items()) {
        bounds = first ? art.bounds : bounds.united(art.bounds);
        first = false;
    }
    return bounds;
}
void shape(Document &document, const QString &kind, QRectF bounds, int tool = 0) {
    document.addShape(kind);
    document.setProperty("x", bounds.x());
    document.setProperty("y", bounds.y());
    document.setProperty("width", bounds.width());
    document.setProperty("height", bounds.height());
    document.setToolLayer(tool);
}
} // namespace
class ArtLibraryUiTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    }
    void realMainLibraryWorkflow_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::newRow("normal-light-mm") << false;
        QTest::newRow("compact-dark-in") << true;
    }
    void realMainLibraryWorkflow() {
        QFETCH(bool, compactDark);
        QTemporaryDir storage;
        Document project;
        project.newProject();
        shape(project, "rect", {10, 20, 20, 10});
        project.setLayer(1);
        project.setObjectSetting("power", 88.);
        project.setObjectSetting("speed", 731.);
        shape(project, "ellipse", {35, 20, 5, 5}, 1);
        shape(project, "rect", {10, 35, 5, 5}, 2);
        project.toggleSelection(0);
        project.toggleSelection(1);
        QCOMPARE(project.selectedRows().size(), 3);
        Machine machine(&project, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(storage.filePath("art.json"));
        ArtLibraryController controller(&project, &machine, &library);
        DeviceManager devices(&project, &machine, storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));
        MaterialTestLedger ledger(storage.filePath("ledger.json"));
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding());
        QVERIFY(services.setDarkTheme(compactDark));
        QVERIFY(services.setLengthUnit(compactDark ? "in" : "mm"));
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("deviceManager", &devices);
        context->setContextProperty("materialLibrary", &materials);
        context->setContextProperty("materialTestLedger", &ledger);
        context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        context->setContextProperty("artLibrary", &library);
        context->setContextProperty("artLibraryController", &controller);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(compactDark ? 1180 : 1480, compactDark ? 760 : 920);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](const QString &name) { return find(window->contentItem(), name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(name); };
        auto evidence = [&](const QString &name) {
            const auto folder = qEnvironmentVariable("BLAZER_ART_LIBRARY_UI_EVIDENCE");
            if (folder.isEmpty())
                return true;
            if (!QDir().mkpath(folder))
                return false;
            QTest::qWait(60);
            return window->grabWindow().save(folder + "/" + name + "-" + QTest::currentDataTag() +
                                             ".png");
        };
        QTRY_VERIFY(object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("acknowledgeStartupSafety")));
        QTRY_VERIFY(!object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("artLibraryButton")));
        auto *dialog = object("artLibraryDialog");
        QVERIFY(dialog);
        auto results = [&] {
            const auto value = dialog->property("results");
            const auto js = value.value<QJSValue>();
            return js.isArray() ? js.toVariant().toList() : value.toList();
        };
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(click(window, item("artLibraryCaptureTab")));
        QVERIFY(click(window, item("artLibraryReviewSelection")));
        QTRY_VERIFY(controller.review()["valid"].toBool());
        QCOMPARE(controller.review()["objectCount"].toInt(), 3);
        QCOMPARE(controller.review()["widthMm"].toDouble(), 30.);
        QCOMPARE(controller.review()["heightMm"].toDouble(), 20.);
        QVERIFY(controller.review()["conversionRequired"].toBool());
        QVERIFY(!item("artLibrarySaveSelection")->isEnabled());
        QVERIFY(evidence("capture-needs-consent"));
        QVERIFY(type(window, item("artLibraryCaptureName"), "Motif A"));
        QVERIFY(type(window, item("artLibraryCaptureTags"), "flower, own"));
        const auto original = project.projectBytes();
        const auto staleToken = controller.review()["token"].toString();
        QVERIFY(!staleToken.isEmpty());
        QVERIFY(click(window, item("artLibraryAcceptConversion")));
        QTRY_VERIFY(item("artLibrarySaveSelection")->isEnabled());
        project.setProperty("x",
                            11.); // Real source edit invalidates both frozen review and UI consent.
        QTRY_VERIFY(!item("artLibraryAcceptConversion")->property("checked").toBool());
        QTRY_VERIFY(!item("artLibrarySaveSelection")->isEnabled());
        QVERIFY(controller.saveSelection("Stale", {}, staleToken, true).isEmpty());
        QVERIFY(library.entries().isEmpty());
        project.undo();
        QCOMPARE(project.projectBytes(), original);
        QVERIFY(click(window, item("artLibraryReviewSelection")));
        QTRY_VERIFY(controller.review()["valid"].toBool());
        QVERIFY(controller.review()["token"].toString() != staleToken);
        QVERIFY(!item("artLibraryAcceptConversion")->property("checked").toBool());
        QVERIFY(click(window, item("artLibraryAcceptConversion")));
        QVERIFY(reveal(window, item("artLibrarySaveSelection")));
        QVERIFY(evidence("capture-consented"));
        QVERIFY(click(window, item("artLibrarySaveSelection")));
        QTRY_COMPARE(library.entries().size(), 1);
        QTRY_COMPARE(item("artLibraryTabs")->property("currentIndex").toInt(), 0);
        const auto a = library.entries().first().toMap()["id"].toString();
        QTRY_COMPARE(dialog->property("selectedAssetId").toString(), a);
        QCOMPARE(project.projectBytes(), original);
        QVERIFY(controller.review().isEmpty());
        for (const auto &entry : library.entries()) {
            const auto row = entry.toMap();
            QVERIFY(!row.contains("geometry"));
            QVERIFY(!row.contains("thumbnailPng"));
        }
        Artwork second;
        second.id = "foreign";
        second.kind = "rect";
        second.bounds = {0, 0, 8, 6};
        second.toolLayer = 2;
        const auto encoded = GeometryAssetCodec::encode({second}, true);
        QVERIFY(encoded.valid());
        const auto b = library.add("Motif B", {"other"}, encoded.bytes);
        QVERIFY(!b.isEmpty());
        QVERIFY(type(window, item("artLibrarySearch"), "flower"));
        QTRY_COMPARE(results().size(), 1);
        QVERIFY(click(window, item("artLibraryRow0")));
        QCOMPARE(dialog->property("selectedAssetId").toString(), a);
        QVERIFY(type(window, item("artLibraryEditName"), "Motif A renamed"));
        QVERIFY(type(window, item("artLibraryEditTags"), "flower, updated"));
        QVERIFY(click(window, item("artLibrarySaveMetadata")));
        QTRY_COMPARE(library.search("updated").size(), 1);
        QCOMPARE(library.search("updated").first().toMap()["id"].toString(), a);
        QVERIFY(type(window, item("artLibrarySearch"), ""));
        QTRY_COMPARE(results().size(), 2);
        QVERIFY(click(window, item("artLibraryRow1")));
        QTRY_COMPARE(dialog->property("selectedAssetId").toString(), b);
        QCOMPARE(item("artLibraryEditName")->property("text").toString(), QString("Motif B"));
        QVERIFY(type(window, item("artLibraryEditName"), "Motif B renamed"));
        QVERIFY(click(window, item("artLibrarySaveMetadata")));
        QTRY_COMPARE(library.search("B renamed").size(), 1);
        QCOMPARE(library.search("updated").first().toMap()["name"].toString(),
                 QString("Motif A renamed"));
        QVERIFY(click(window, item("artLibraryRow0")));
        QTRY_COMPARE(dialog->property("selectedAssetId").toString(), a);
        QVERIFY(click(window, item("artLibraryClose")));
        project.newProject(); // Insert into an empty real document, not over the source output.
        const auto profile = project.deviceInfo()["outputFingerprint"];
        QVERIFY(click(window, item("artLibraryButton")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto numeric = [&](const char *name) {
            return item(name)->findChild<QQuickItem *>("numericEditor");
        };
        auto *x = numeric("artLibraryInsertX");
        auto *y = numeric("artLibraryInsertY");
        QVERIFY(x);
        QVERIFY(y);
        // Raw invalid syntax and bed violations must never silently reuse committed zero.
        for (const auto &bad : QStringList{"", "NaN", "1 foo", "Infinity", "-1 mm", "200 mm"}) {
            QVERIFY(type(window, x, bad));
            QTRY_VERIFY(!item("artLibraryInsert")->isEnabled());
            QVERIFY(click(window, item("artLibraryInsert"), false));
            QCOMPARE(project.count(), 0);
            QVERIFY(dialog->property("visible").toBool());
        }
        QVERIFY(type(window, x, "1 in"));
        for (const auto &bad : QStringList{"NaN", "150 mm"}) {
            QVERIFY(type(window, y, bad));
            QTRY_VERIFY(!item("artLibraryInsert")->isEnabled());
            QVERIFY(click(window, item("artLibraryInsert"), false));
            QCOMPARE(project.count(), 0);
            QVERIFY(dialog->property("visible").toBool());
        }
        QVERIFY(type(window, y, "2.5 mm"));
        QTRY_VERIFY(item("artLibraryInsert")->isEnabled());
        QVERIFY(reveal(window, item("artLibraryInsert")));
        QVERIFY(evidence("insert-valid-mixed-units"));
        QVERIFY(click(window, item("artLibraryInsert")));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(project.count(), 3);
        QCOMPARE(allBounds(project), QRectF(25.4, 2.5, 30, 20));
        QCOMPARE(project.selectionBounds(), QRectF(25.4, 2.5, 20, 10));
        QCOMPARE(project.selectedRows().size(),
                 1); // Only original graphics are offered for local assignment.
        QCOMPARE(project.deviceInfo()["outputFingerprint"], profile);
        for (int i = 0; i < project.items().size(); ++i) {
            const auto &art = project.items()[i];
            QCOMPARE(art.toolLayer, i == 2 ? 2 : 1);
            QVERIFY(art.settings.isEmpty());
            QVERIFY(art.extraOperations.isEmpty());
        }
        QVERIFY(!project.job().valid());
        const auto blocked = QUrl::fromLocalFile(storage.filePath("unassigned.gcode"));
        QVERIFY(!project.exportGcode(blocked));
        QVERIFY(!QFile::exists(blocked.toLocalFile()));
        project.undo();
        QCOMPARE(project.count(), 0);
        project.redo();
        QCOMPARE(project.count(), 3);
        QCOMPARE(allBounds(project), QRectF(25.4, 2.5, 30, 20));
        QCOMPARE(project.selectionBounds(), QRectF(25.4, 2.5, 20, 10));
        QCOMPARE(project.selectedRows().size(),
                 1); // Only original graphics are offered for local assignment.
        QVERIFY(!project.job().valid());
        QVERIFY(project.setToolLayer(0));
        project.setLayer(1); // Explicit local operation assignment is separate.
        QVERIFY2(project.job().valid(), qPrintable(project.job().errors.join(";")));
        QCOMPARE(project.items()[1].toolLayer, 1);
        QCOMPARE(project.items()[2].toolLayer, 2);
        QVERIFY(project.exportGcode(QUrl::fromLocalFile(storage.filePath("assigned.gcode"))));
        project.newProject();
        QVERIFY(services.setLengthUnit("mm"));
        QVERIFY(click(window, item("artLibraryButton")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(type(window, x, "1"));         // Draft begins as millimetres.
        QVERIFY(services.setLengthUnit("in")); // Change units before blur/Insert.
        QVERIFY(type(window, y, "2 mm"));
        QTRY_VERIFY(item("artLibraryInsert")->isEnabled());
        QVERIFY(click(window, item("artLibraryInsert")));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(allBounds(project), QRectF(1, 2, 30, 20));
        QCOMPARE(project.selectionBounds(), QRectF(1, 2, 20, 10));
        QVERIFY(!project.job().valid());
        const auto unchanged = project.projectBytes();
        QVERIFY(click(window, item("artLibraryButton")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(click(window, item("artLibraryDelete")));
        QCOMPARE(library.entries().size(), 2);
        QVERIFY(click(
            window, item("artLibraryRow1"))); // An armed delete must not follow a different asset.
        QVERIFY(click(window, item("artLibraryDelete")));
        QCOMPARE(library.entries().size(), 2);
        QVERIFY(click(window, item("artLibraryRow0")));
        QVERIFY(click(window, item("artLibraryDelete")));
        QCOMPARE(library.entries().size(), 2);
        QVERIFY(click(window, item("artLibraryDelete")));
        QTRY_COMPARE(library.entries().size(), 1);
        QCOMPARE(library.entries().first().toMap()["id"].toString(), b);
        QCOMPARE(project.projectBytes(), unchanged);
        QVERIFY(evidence("library-after-delete"));
        QVERIFY(click(window, item("artLibraryClose")));
        QVERIFY(machine.simulation());
        QVERIFY(machine.ports().isEmpty());
        window->setProperty("discardCloseApproved", true);
        window->close();
    }
};
QTEST_MAIN(ArtLibraryUiTest)
#include "artlibraryui_test.moc"
