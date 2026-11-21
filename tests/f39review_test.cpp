#include "applicationservices.h"
#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "materiallibrary.h"
#include "materialtestledger.h"
#include "registration.h"
#include "reliefpreview.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void rectangle(Document &document) {
    document.newProject();
    document.addShape("rect");
    document.setLayer(1);
    document.setProperty("x", 10.);
    document.setProperty("y", 20.);
    document.setProperty("width", 40.);
    document.setProperty("height", 20.);
}
QRectF bounds(const QVariantMap &map) {
    return {map["x"].toDouble(), map["y"].toDouble(), map["width"].toDouble(),
            map["height"].toDouble()};
}
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = find(child, name))
            return found;
    return nullptr;
}
bool click(QQuickWindow *window, QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled())
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
    QTest::qWait(30);
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, {}, point);
    return true;
}
} // namespace
class F39ReviewTest : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    }
    void renderedWorkspaceUsesPlacedMovesRatherThanEditorBounds() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        Workspace workspace;
        workspace.setWidth(800);
        workspace.setHeight(650);
        workspace.setDocument(&document);
        workspace.setPreview(true);
        auto render = [&] {
            QImage image(800, 650, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            workspace.paint(&painter);
            painter.end();
            return image;
        };
        auto blueBounds = [&](const QImage &image) {
            int left = image.width(), right = -1, top = image.height(), bottom = -1;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    if (color.blue() - color.red() > 60 && color.blue() - color.green() > 25) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            return right >= left ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect();
        };
        const auto absolute = render();
        const auto originalPixels = blueBounds(absolute);
        QVERIFY(!originalPixels.isEmpty());
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        const auto current = render();
        const auto placedPixels = blueBounds(current);
        QVERIFY(!placedPixels.isEmpty());
        QVERIFY(placedPixels.left() > originalPixels.right() + 20);
        QVERIFY(std::abs(placedPixels.width() - originalPixels.width()) <= 2);
        const double scale = double(originalPixels.width() - 1) / 40.;
        QVERIFY(std::abs((placedPixels.center().x() - originalPixels.center().x()) / scale - 70.) <
                1.);
        QVERIFY(std::abs((placedPixels.center().y() - originalPixels.center().y()) / scale + 45.) <
                1.);
        QCOMPARE(document.items().first().bounds, QRectF(10, 20, 40, 20));
        const auto folder = qEnvironmentVariable("BLAZER_F39_REVIEW_EVIDENCE");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QVERIFY(absolute.save(folder + "/preview-absolute.png"));
            QVERIFY(current.save(folder + "/preview-current.png"));
        }
    }
    void staleFrameMapCannotRepositionOldJobAfterHeadRevisionChanged() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        const auto oldFrame = document.framingBounds("job");
        QCOMPARE(bounds(oldFrame), QRectF(80, 65, 40, 20));
        machine.jog(20, 10);
        QCOMPARE(bounds(document.framingBounds("job")), QRectF(100, 75, 40, 20));
        machine.frame(oldFrame);
        QVERIFY2(!machine.busy(),
                 "Old same-profile framing map was executed after head/source revision changed");
    }
    void staleFrameRejectsSameBoundsChangedPower() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        const auto oldFrame = document.framingBounds("job");
        document.setLayerSetting(1, "power", 33.);
        QCOMPARE(bounds(oldFrame), bounds(document.framingBounds("job")));
        machine.frame(oldFrame);
        QVERIFY2(!machine.busy(), "Same bounding box did not make changed job evidence fresh");
    }
    void frameRejectsDifferentDocumentIdentity() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        Document other;
        rectangle(other);
        const auto foreign = other.framingBounds("job");
        QCOMPARE(bounds(foreign), bounds(document.framingBounds("job")));
        machine.frame(foreign);
        QVERIFY2(!machine.busy(), "A framing map from another document was accepted");
    }
    void frameRejectsTamperedCoordinates() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        auto tampered = document.framingBounds("job");
        tampered["x"] = tampered["x"].toDouble() + 10;
        machine.frame(tampered);
        QVERIFY2(!machine.busy(),
                 "Modified coordinate map was accepted instead of canonical target");
    }
    void completedCurrentPositionFrameMustNotMoveNextJobOutsideTheAreaJustFramed() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        const auto framed = bounds(document.framingBounds("job"));
        QCOMPARE(framed, QRectF(80, 65, 40, 20));
        machine.frame(document.framingBounds("job"));
        QVERIFY(machine.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!machine.busy(), 6000);
        QCOMPARE(bounds(document.framingBounds("job")), framed);
    }
    void providerIsFrozenWhilePausedOrFocusedAndFaultRevokesOrigin() {
        Document document;
        rectangle(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(document.captureUserOrigin());
        const auto frozen = document.planningInput().placement.startPosition;
        QVERIFY(frozen.has_value());
        machine.focus(true);
        QVERIFY(machine.firing());
        const auto focused = document.planningInput().placement.startPosition;
        QCOMPARE(focused->revision, frozen->revision);
        QCOMPARE(focused->coordinateFrameFingerprint, frozen->coordinateFrameFingerprint);
        machine.focus(false);
        machine.start();
        QVERIFY(machine.busy());
        QTest::qWait(100);
        machine.pause();
        QVERIFY(machine.paused());
        const auto paused = document.planningInput().placement.startPosition;
        QCOMPARE(paused->revision, frozen->revision);
        QCOMPARE(paused->point, frozen->point);
        machine.refreshPreviewPosition();
        QCOMPARE(document.planningInput().placement.startPosition->revision, frozen->revision);
        machine.stop();
        QVERIFY(document.hasPreviewPosition());
        const auto intent = document.projectBytes();
        machine.disconnectFault();
        QVERIFY(!document.hasPreviewPosition());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(!document.job().valid());
        QCOMPARE(document.projectBytes(), intent);
        machine.toggleConnection();
        QVERIFY(document.hasPreviewPosition());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(!document.job().valid());
        QVERIFY(document.confirmUserOrigin());
        QVERIFY(document.job().valid());
        machine.useSimulator();
        QVERIFY(document.hasPreviewPosition());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
    }
    void freshApplicationCannotReuseAnotherMachineUserFrameAndExportMatchesNewPreview() {
        QTemporaryDir storage;
        const auto file = QUrl::fromLocalFile(storage.filePath("intent.blazer"));
        QByteArray firstFrame;
        {
            Document document;
            rectangle(document);
            Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
            QVERIFY(document.setStartMode(Document::UserOrigin));
            QVERIFY(document.setJobOrigin(Document::Center));
            machine.jog(10, 5);
            QVERIFY(document.captureUserOrigin());
            QVERIFY(document.save(file));
            firstFrame =
                document.planningInput().placement.startPosition->coordinateFrameFingerprint;
        }
        Document reopened;
        QVERIFY(reopened.open(file));
        Machine machine(&reopened, nullptr, Machine::HardwareAccess::Disabled);
        QVERIFY(reopened.hasPreviewPosition());
        QVERIFY(reopened.planningInput().placement.startPosition->coordinateFrameFingerprint !=
                firstFrame);
        QVERIFY(!reopened.job().valid());
        QVERIFY(!reopened.placementSummary()["originConfirmed"].toBool());
        const auto blocked = QUrl::fromLocalFile(storage.filePath("unconfirmed.gcode"));
        QVERIFY(!reopened.exportGcode(blocked));
        QVERIFY(!QFile::exists(blocked.toLocalFile()));
        QVERIFY(reopened.confirmUserOrigin());
        QCOMPARE(bounds(reopened.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(90, 70, 40, 20));
        const auto output = QUrl::fromLocalFile(storage.filePath("confirmed.gcode"));
        QVERIFY(reopened.exportGcode(output));
        QFile encoded(output.toLocalFile());
        QVERIFY(encoded.open(QIODevice::ReadOnly));
        QCOMPARE(encoded.readAll(), reopened.job().encodedProgram().payload());
        QVERIFY(machine.simulation());
        QVERIFY(machine.ports().isEmpty());
    }
    void realMainOpensPlacementAndChangesActualPipeline_data() {
        QTest::addColumn<bool>("compactDark");
        QTest::addColumn<bool>("background");
        QTest::newRow("light-sync") << false << false;
        QTest::newRow("compact-dark-sync") << true << false;
        QTest::newRow("light-async") << false << true;
        QTest::newRow("compact-dark-async") << true << true;
    }
    void realMainOpensPlacementAndChangesActualPipeline() {
        QFETCH(bool, compactDark);
        QFETCH(bool, background);
        QTemporaryDir storage;
        Document project;
        rectangle(project);
        project.setBackgroundPlanning(background);
        Machine machine(&project, nullptr, Machine::HardwareAccess::Disabled);
        DeviceManager devices(&project, &machine, storage.filePath("devices.json"));
        MaterialLibrary materials(storage.filePath("materials.json"));
        MaterialTestLedger ledger(storage.filePath("ledger.json"));
        Registration registration;
        ApplicationServices services(&project, &machine, storage.filePath("settings.ini"));
        QVERIFY(services.finishOnboarding());
        QVERIFY(services.setDarkTheme(compactDark));
        QQmlApplicationEngine engine;
        auto *context = engine.rootContext();
        context->setContextProperty("project", &project);
        context->setContextProperty("machine", &machine);
        context->setContextProperty("deviceManager", &devices);
        context->setContextProperty("materialLibrary", &materials);
        context->setContextProperty("materialTestLedger", &ledger);
        context->setContextProperty("cameraRegistration", &registration);
        context->setContextProperty("appServices", &services);
        engine.load(QUrl::fromLocalFile(QString(BLAZER_SOURCE_DIR) + "/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(compactDark ? 1180 : 1480, compactDark ? 760 : 920);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto item = [&](const QString &name) { return find(window->contentItem(), name); };
        auto object = [&](const char *name) { return window->findChild<QObject *>(name); };
        QTRY_VERIFY(object("startupSafetyDialog")->property("visible").toBool());
        QVERIFY(click(window, item("acknowledgeStartupSafety")));
        QTRY_VERIFY(!object("startupSafetyDialog")->property("visible").toBool());
        QTRY_VERIFY(project.job().valid());
        QVERIFY(click(window, item("jobPlacementButton")));
        QTRY_VERIFY(object("jobPlacementDialog")->property("visible").toBool());
        machine.disconnectFault();
        QTRY_VERIFY(!project.hasPreviewPosition());
        QTRY_VERIFY(!item("refreshPlacementPosition")->isEnabled());
        const auto unavailable = item("previewHeadPosition")->property("text").toString();
        QVERIFY(unavailable.contains("Simulator"));
        QVERIFY(!unavailable.contains("reales Gerät"));
        machine.toggleConnection();
        QTRY_VERIFY(project.hasPreviewPosition());
        QTRY_VERIFY(project.job().valid());
        auto *choice = item("startModeChoice");
        QVERIFY(choice);
        QVERIFY(click(window, choice));
        QTest::keyClick(window, Qt::Key_Home);
        QTest::keyClick(window, Qt::Key_Down);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project.startMode(), Document::CurrentPosition);
        const QList<QRectF> anchors{{100, 75, 40, 20}, {80, 75, 40, 20}, {60, 75, 40, 20},
                                    {100, 65, 40, 20}, {80, 65, 40, 20}, {60, 65, 40, 20},
                                    {100, 55, 40, 20}, {80, 55, 40, 20}, {60, 55, 40, 20}};
        for (int index = 0; index < 9; ++index) {
            QVERIFY(click(window, item("jobOrigin" + QString::number(index))));
            QCOMPARE(int(project.jobOrigin()), index);
            QTRY_VERIFY(project.job().valid());
            QCOMPARE(bounds(project.placementSummary()["graphicsBounds"].toMap()), anchors[index]);
        }
        QVERIFY(click(window, item("jobOrigin4")));
        QTRY_VERIFY(project.job().valid());
        QCOMPARE(project.jobOrigin(), Document::Center);
        QCOMPARE(bounds(project.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(80, 65, 40, 20));
        const auto original = project.items().first().bounds;
        QCOMPARE(original, QRectF(10, 20, 40, 20));
        QVERIFY(click(window, choice));
        QTest::keyClick(window, Qt::Key_End);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(project.startMode(), Document::UserOrigin);
        QVERIFY(!project.job().valid());
        QVERIFY(click(window, item("captureJobUserOrigin")));
        QTRY_VERIFY(project.job().valid());
        QVERIFY(project.placementSummary()["originConfirmed"].toBool());
        const auto saved = QUrl::fromLocalFile(storage.filePath("from-ui.blazer"));
        QVERIFY(project.save(saved));
        QVERIFY(project.open(saved));
        QVERIFY(!project.job().valid());
        QTRY_VERIFY(item("confirmJobUserOrigin")->isEnabled());
        const auto evidence = qEnvironmentVariable("BLAZER_F39_REVIEW_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(evidence + "/placement-dialog-unconfirmed-" +
                                              QTest::currentDataTag() + ".png"));
        }
        QVERIFY(click(window, item("confirmJobUserOrigin")));
        QTRY_VERIFY(project.job().valid());
        QVERIFY(QMetaObject::invokeMethod(object("jobPlacementDialog"), "close"));
        machine.jog(10, 0);
        QTRY_VERIFY(project.job().valid());
        QCOMPARE(bounds(project.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(80, 65, 40, 20));
        QCOMPARE(project.job().moves.first().from, QPointF(110, 75));
        QVERIFY(machine.simulation());
        QVERIFY(machine.ports().isEmpty());
        QCOMPARE(window->property("page").toInt(), 0);
        QVERIFY(click(window, item("startJobButton")));
        QVERIFY(!machine.busy()); // Preview action must not start even simulated processing.
        QTRY_VERIFY(qobject_cast<Workspace *>(item("workspace"))->preview());
        const auto folder = qEnvironmentVariable("BLAZER_F39_REVIEW_EVIDENCE");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QTest::qWait(80);
            QVERIFY(window->grabWindow().save(folder + "/main-placement-" +
                                              QTest::currentDataTag() + ".png"));
        }
        window->setProperty("discardCloseApproved", true);
        window->close();
    }
};
QTEST_MAIN(F39ReviewTest)
#include "f39review_test.moc"
