#include "artlibrary.h"
#include "artlibrarycontroller.h"
#include "document.h"
#include "machine.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <limits>
class ArtLibraryControllerTest : public QObject {
    Q_OBJECT
    QByteArray asset() {
        Artwork graphic;
        graphic.id = "graphic";
        graphic.kind = "rect";
        graphic.bounds = {10, 20, 10, 10};
        graphic.settings = {{"power", 100}, {"speed", 1}, {"passes", 100}};
        graphic.extraOperations = {1};
        graphic.groupId = "group";
        graphic.groupName = "source group";
        auto t1 = graphic;
        t1.id = "helper1";
        t1.bounds = {25, 20, 5, 5};
        t1.toolLayer = 1;
        auto t2 = graphic;
        t2.id = "helper2";
        t2.bounds = {10, 35, 5, 5};
        t2.toolLayer = 2;
        const auto encoded = GeometryAssetCodec::encode({graphic, t1, t2}, true);
        if (!encoded.valid())
            qFatal("bad fixture");
        return encoded.bytes;
    }
    void prepare(Document &document) {
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        document.setBasicOperation("outline");
    }
  private slots:
    void documentInsertionIsOneUndoWithIndependentBoundsRolesAndDestinationParameters() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        QVERIFY(document.setMetadata("Destination title", "destination notes"));
        QVERIFY(document.setJobOrigin(Document::UpperRight));
        QVERIFY(document.save(QUrl::fromLocalFile(directory.filePath("destination.blazer"))));
        const auto before = document.projectBytes(), sourceId = document.items()[0].id.toUtf8();
        const auto layers = document.layers();
        const auto material = document.material();
        const auto profile = document.deviceProfile().toJson();
        const auto file = document.fileUrl();
        const auto code = document.job().gcode();
        const auto frozen = document.job();
        const auto history = document.historyPosition();
        QVERIFY(document.insertArtGeometry(asset(), 100, 80));
        QCOMPARE(document.historyPosition(), history + 1);
        QCOMPARE(document.count(), 4);
        QCOMPARE(document.items()[1].bounds, QRectF(100, 80, 10, 10));
        QCOMPARE(document.items()[2].bounds, QRectF(115, 80, 5, 5));
        QCOMPARE(document.items()[3].bounds, QRectF(100, 95, 5, 5));
        QCOMPARE(document.items()[1].toolLayer, 1);
        QCOMPARE(document.items()[2].toolLayer, 1);
        QCOMPARE(document.items()[3].toolLayer, 2);
        QCOMPARE(document.selectedRows(), QVariantList({1}));
        QCOMPARE(document.items()[0].id.toUtf8(), sourceId);
        QVERIFY(document.items()[1].groupId != QString("group"));
        QCOMPARE(document.items()[1].groupId, document.items()[3].groupId);
        for (int i = 1; i < 4; ++i) {
            QVERIFY(document.items()[i].settings.isEmpty());
            QVERIFY(document.items()[i].extraOperations.isEmpty());
        }
        QCOMPARE(document.layers(), layers);
        QCOMPARE(document.material(), material);
        QCOMPARE(document.deviceProfile().toJson(), profile);
        QCOMPARE(document.fileUrl(), file);
        QCOMPARE(document.jobOrigin(), Document::UpperRight);
        QCOMPARE(document.job().gcode(), code);
        QCOMPARE(frozen.gcode(), code);
        const auto inserted = document.projectBytes();
        const auto firstGroup = document.items()[1].groupId;
        QVERIFY(document.insertArtGeometry(asset(), 140, 100));
        QCOMPARE(document.historyPosition(), history + 2);
        QVERIFY(document.items()[4].groupId != firstGroup);
        QVERIFY(document.items()[4].id != document.items()[1].id);
        document.undo();
        QCOMPARE(document.projectBytes(), inserted);
        document.undo();
        QCOMPARE(document.projectBytes(), before);
        document.redo();
        QCOMPARE(document.projectBytes(), inserted);
        document.setBasicOperation("outline");
        QCOMPARE(document.items()[1].toolLayer, 0);
        QCOMPARE(document.items()[2].toolLayer, 1);
        QCOMPARE(document.items()[3].toolLayer, 2);
        QVERIFY(document.job().gcode() != code);
    }
    void documentFailuresAreAtomicAndSelectionOnlyScopeNeverEnablesImportedGeometry() {
        Document document;
        prepare(document);
        const auto before = document.projectBytes();
        const auto history = document.historyRevision();
        for (const auto point : QList<QPointF>{
                 {-1, 0}, {195, 0}, {0, 145}, {std::numeric_limits<double>::quiet_NaN(), 0}}) {
            QVERIFY(!document.insertArtGeometry(asset(), point.x(), point.y()));
            QCOMPARE(document.projectBytes(), before);
            QCOMPARE(document.historyRevision(), history);
        }
        QVERIFY(!document.insertArtGeometry("{}", 0, 0));
        QCOMPARE(document.projectBytes(), before);
        document.beginMove();
        QVERIFY(!document.insertArtGeometry(asset(), 0, 0));
        QCOMPARE(document.projectBytes(), before);
        document.finishMove();
        document.setCutSelectedGraphics(true);
        QVERIFY(document.job().valid());
        const auto frozen = document.job();
        QVERIFY(document.insertArtGeometry(asset(), 100, 80));
        QVERIFY(document.cutSelectedGraphics());
        QVERIFY(!document.job().valid());
        QVERIFY(document.job().gcode().isEmpty());
        QVERIFY(frozen.valid());
    }
    void activePageAndCapacityChecksPreserveWholeDocument() {
        Document document;
        prepare(document);
        QVERIFY(document.addPage("target"));
        QVERIFY(document.insertArtGeometry(asset(), 20, 20));
        for (int i = 1; i < document.items().size(); ++i)
            QCOMPARE(document.items()[i].page, 1);
        Document full;
        prepare(full);
        auto root = QJsonDocument::fromJson(full.projectBytes()).object();
        auto prototype = root["objects"].toArray().first().toObject();
        QJsonArray objects;
        for (int i = 0; i < 2000; ++i) {
            auto object = prototype;
            object["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            objects.append(object);
        }
        root["objects"] = objects;
        QVERIFY(full.restoreBytes(QJsonDocument(root).toJson()));
        const auto before = full.projectBytes();
        const auto revision = full.historyRevision();
        QVERIFY(!full.insertArtGeometry(asset(), 0, 0));
        QCOMPARE(full.projectBytes(), before);
        QCOMPARE(full.historyRevision(), revision);
    }
    void controllerReviewRequiresExplicitAcceptanceAndNeverRecapturesChangedSource() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        const auto before = document.projectBytes();
        const auto review = controller.reviewSelection();
        QVERIFY(review["valid"].toBool());
        QVERIFY(review["conversionRequired"].toBool());
        QVERIFY(review["thumbnailSource"].toString().startsWith("data:image/png;base64,"));
        const auto token = review["token"].toString();
        QCOMPARE(document.projectBytes(), before);
        QVERIFY(library.entries().isEmpty());
        document.notify("unrelated planning/status message");
        QCOMPARE(controller.review()["token"].toString(), token);
        document.setLayerSetting(0, "power", 17.);
        QCOMPARE(controller.review()["token"].toString(),
                 token); // Source operations are not stored.
        QVERIFY(controller.saveSelection("No permission", {}, token, false).isEmpty());
        QVERIFY(library.entries().isEmpty());
        document.setProperty("x", 42.);
        QVERIFY(controller.review().isEmpty());
        QVERIFY(controller.saveSelection("Stale", {}, token, true).isEmpty());
        QVERIFY(library.entries().isEmpty());
        const auto fresh = controller.reviewSelection();
        QVERIFY(fresh["token"].toString() != token);
        const auto id =
            controller.saveSelection("Current", {"tag"}, fresh["token"].toString(), true);
        QVERIFY2(!id.isEmpty(), qPrintable(controller.message()));
        QCOMPARE(library.entries().size(), 1);
        QVERIFY(controller.review().isEmpty());
        QVERIFY(
            controller.saveSelection("Consumed", {}, fresh["token"].toString(), true).isEmpty());
        QCOMPARE(library.entries().size(), 1);
        QVERIFY(controller.updateMetadata(id, "Renamed", {"new"}));
        QCOMPARE(library.entries().first().toMap()["name"].toString(), QString("Renamed"));
    }
    void differentIdentityUnselectedEditsClearAndNewReviewsHaveExactTokens() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        document.addShape("ellipse");
        document.select(0);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        const auto first = controller.reviewSelection();
        const auto token = first["token"].toString();
        document.select(1);
        QVERIFY(controller.review().isEmpty());
        document.select(0);
        QVERIFY(controller.saveSelection("Old identity", {}, token, true).isEmpty());
        const auto second = controller.reviewSelection(), third = controller.reviewSelection();
        QVERIFY(second["token"] != third["token"]);
        QVERIFY(
            controller.saveSelection("Old review", {}, second["token"].toString(), true).isEmpty());
        controller.clearReview();
        QVERIFY(controller.saveSelection("Cleared", {}, third["token"].toString(), true).isEmpty());
        const auto current = controller.reviewSelection();
        auto modified = QJsonDocument::fromJson(document.projectBytes()).object();
        auto objects = modified["objects"].toArray();
        auto unselected = objects[1].toObject();
        unselected["x"] = unselected["x"].toDouble() + 7;
        objects[1] = unselected;
        modified["objects"] = objects;
        QVERIFY(document.restoreBytes(QJsonDocument(modified).toJson()));
        QCOMPARE(controller.review()["token"],
                 current["token"]); // Unselected geometry is unrelated.
        QVERIFY(document.setMetadata("Changed title", "Unrelated notes"));
        QCOMPARE(controller.review()["token"], current["token"]);
        const auto id =
            controller.saveSelection("Unaffected", {}, current["token"].toString(), true);
        QVERIFY(!id.isEmpty());
        QVERIFY(controller.insert(id, 100, 80));
        QVERIFY(document.items().last().toolLayer > 0);
        QVERIFY(controller.remove(id));
        QVERIFY(!controller.insert(id, 0, 0));
    }
    void controllerBlocksBusyFocusAndPlanningAndDoesNotRenderNewThumbnails() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        const auto id = library.add("Asset", {}, asset()),
                   uncached = library.add("Not rendered", {}, asset());
        QVERIFY(!id.isEmpty());
        QVERIFY(!uncached.isEmpty());
        const auto cached = controller.thumbnailSource(id);
        QVERIFY(!cached.isEmpty());
        const auto before = document.projectBytes();
        machine.start();
        QVERIFY(machine.busy());
        QVERIFY(!controller.blockReason().isEmpty());
        QVERIFY(!controller.insert(id, 20, 20));
        QCOMPARE(document.projectBytes(), before);
        QCOMPARE(controller.thumbnailSource(id), cached);
        QVERIFY(controller.thumbnailSource(uncached).isEmpty());
        machine.stop();
        machine.focus(true);
        QVERIFY(machine.firing());
        QVERIFY(!controller.insert(id, 20, 20));
        QCOMPARE(document.projectBytes(), before);
        machine.focus(false);
        document.setBackgroundPlanning(true);
        document.retryPlanning();
        QVERIFY(document.planningBusy());
        QSignalSpy changed(&controller, &ArtLibraryController::changed);
        QVERIFY(!controller.insert(id, 20, 20));
        QVERIFY(controller.thumbnailSource(uncached).isEmpty());
        QTRY_VERIFY(!document.planningBusy());
        QVERIFY(changed.count() > 0);
        QVERIFY(controller.blockReason().isEmpty());
    }
    void controllerBlocksBatchAndImageExportWithoutMutatingDocument() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        const auto id = library.add("Asset", {}, asset());
        QVERIFY(!id.isEmpty());
        const auto review = controller.reviewSelection();
        QVERIFY(review["valid"].toBool());
        const auto csv = directory.filePath("input.csv");
        {
            QFile file(csv);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("name\nA\nB\n");
        }
        const auto output = directory.filePath("batch");
        QVERIFY(QDir().mkdir(output));
        document.setBackgroundPlanning(true);
        QTRY_VERIFY(!document.planningBusy());
        QVERIFY(
            document.batch(QUrl::fromLocalFile(csv), QUrl::fromLocalFile(output), "row-{name}"));
        QVERIFY(document.batchBusy());
        const auto before = document.projectBytes();
        QVERIFY(!controller.insert(id, 0, 0));
        QCOMPARE(document.projectBytes(), before);
        QVERIFY(controller.saveSelection("Busy", {}, review["token"].toString(), true).isEmpty());
        QCOMPARE(library.entries().size(), 1);
        QTRY_VERIFY(!document.batchBusy());
        QImage pixels(16, 16, QImage::Format_ARGB32);
        pixels.fill(Qt::black);
        const auto source = directory.filePath("source.png");
        QVERIFY(pixels.save(source));
        QVERIFY(document.importImage(QUrl::fromLocalFile(source)));
        QVERIFY(document.exportImage(QUrl::fromLocalFile(directory.filePath("export.png")), false));
        QVERIFY(document.imageExportBusy());
        const auto withImage = document.projectBytes();
        QVERIFY(!controller.insert(id, 0, 0));
        QCOMPARE(document.projectBytes(), withImage);
        QVERIFY(controller.thumbnailSource(id).isEmpty());
        QTRY_VERIFY(!document.imageExportBusy());
        QTRY_VERIFY(controller.blockReason().isEmpty());
    }
    void tinyBoundsAndPathChangesInvalidateTheExactSourceReview() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        auto review = controller.reviewSelection();
        QVERIFY(review["valid"].toBool());
        auto root = QJsonDocument::fromJson(document.projectBytes()).object();
        auto objects = root["objects"].toArray();
        auto object = objects[0].toObject();
        const double before = object["x"].toDouble();
        object["x"] = before + 1e-12;
        objects[0] = object;
        root["objects"] = objects;
        QVERIFY(document.restoreBytes(QJsonDocument(root).toJson()));
        QVERIFY(document.items()[0].bounds.x() != before);
        QVERIFY(controller.review().isEmpty());
        QVERIFY(controller.saveSelection("Stale tiny bounds", {}, review["token"].toString(), true)
                    .isEmpty());
        root = QJsonDocument::fromJson(document.projectBytes()).object();
        objects = root["objects"].toArray();
        object = objects[0].toObject();
        object["kind"] = "path";
        object["path"] =
            QJsonArray{QJsonArray{0, 0., 0.}, QJsonArray{1, 1., 0.}, QJsonArray{1, 1., 1.},
                       QJsonArray{1, 0., 1.}, QJsonArray{1, 0., 0.}};
        objects[0] = object;
        root["objects"] = objects;
        QVERIFY(document.restoreBytes(QJsonDocument(root).toJson()));
        review = controller.reviewSelection();
        QVERIFY(review["valid"].toBool());
        auto path = object["path"].toArray();
        auto point = path[0].toArray();
        point[1] = 1e-13;
        path[0] = point;
        object["path"] = path;
        objects[0] = object;
        root["objects"] = objects;
        QVERIFY(document.restoreBytes(QJsonDocument(root).toJson()));
        QVERIFY(document.items()[0].path.elementAt(0).x != 0.);
        QVERIFY(controller.review().isEmpty());
        QVERIFY(controller.saveSelection("Stale tiny path", {}, review["token"].toString(), true)
                    .isEmpty());
        QVERIFY(library.entries().isEmpty());
    }
    void sourceAndInsertionReentryCannotCreateExtraRecordsOrUndoSteps() {
        QTemporaryDir directory;
        Document document;
        prepare(document);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(directory.filePath("art.blazerart"));
        ArtLibraryController controller(&document, &machine, &library);
        const auto prepared = controller.reviewSelection();
        bool nestedSaved = false;
        bool callback = false;
        connect(&library, &ArtLibrary::changed, &controller, [&] {
            if (callback)
                return;
            callback = true;
            nestedSaved =
                !controller.saveSelection("Nested", {}, prepared["token"].toString(), true)
                     .isEmpty();
        });
        const auto id = controller.saveSelection("One", {}, prepared["token"].toString(), true);
        QVERIFY(!id.isEmpty());
        QVERIFY(callback);
        QVERIFY(!nestedSaved);
        QCOMPARE(library.entries().size(), 1);
        const auto history = document.historyPosition();
        bool attempted = false, nestedInsert = false;
        connect(&document, &QAbstractItemModel::modelAboutToBeReset, &controller, [&] {
            if (attempted)
                return;
            attempted = true;
            nestedInsert = document.insertArtGeometry(asset(), 0, 0);
        });
        QVERIFY(controller.insert(id, 100, 80));
        QVERIFY(attempted);
        QVERIFY(!nestedInsert);
        QCOMPARE(document.historyPosition(), history + 1);
    }
};
QTEST_MAIN(ArtLibraryControllerTest)
#include "artlibrarycontroller_test.moc"
