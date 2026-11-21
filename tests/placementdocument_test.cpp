#include "document.h"
#include "machine.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cmath>
using namespace JobPlacement;

class PlacementDocumentTest : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    static void rectangle(Document &document, double x = 10, double y = 20) {
        document.addShape("rect");
        document.setProperty("x", x);
        document.setProperty("y", y);
        document.setProperty("width", 20.);
        document.setProperty("height", 10.);
    }
    static PositionSnapshot position(const Document &document, QPointF point, quint64 revision = 1,
                                     QByteArray frame = QByteArray(64, 'b')) {
        return {point, true, document.deviceProfile().outputFingerprint(), frame, revision};
    }
    static QByteArray read(const QUrl &file) {
        QFile input(file.toLocalFile());
        return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray();
    }
    static QRectF rectangle(const QVariantMap &map) {
        return {map["x"].toDouble(), map["y"].toDouble(), map["width"].toDouble(),
                map["height"].toDouble()};
    }
  private slots:
    void selectionOriginInactiveWithoutMaskAndUndoDoesNotRestoreConfirmation() {
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 50, 40);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        const auto all = document.job().encodedProgram().payload();
        document.setUseSelectionOrigin(true);
        QCOMPARE(document.job().encodedProgram().payload(), all);
        QVERIFY(!document.placementSummary()["selectionOriginActive"].toBool());
        document.select(-1);
        QCOMPARE(document.job().encodedProgram().payload(), all);
        document.select(1);
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setStartMode(Document::UserOrigin));
        document.setCutSelectedGraphics(true);
        QVERIFY(document.job().valid());
        document.undo();
        QVERIFY(!document.cutSelectedGraphics());
        QVERIFY(document.useSelectionOrigin());
        QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
        QVERIFY(!document.job().valid());
        document.redo();
        QVERIFY(document.cutSelectedGraphics());
        QVERIFY(!document.job().valid());
        QVERIFY(document.confirmUserOrigin());
        QVERIFY(document.job().valid());
    }
    void slowSelectionWorkerCannotPublishPreviousMembership() {
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 60, 40);
        document.setCutSelectedGraphics(true);
        auto first = document.planningInput();
        std::atomic_bool started = false;
        JobPreparation preparation(nullptr, [&](const JobInput &input, const Cancellation &cancel) {
            if (!started.exchange(true))
                while (!isCancelled(cancel))
                    QThread::msleep(1);
            return JobPlanner::build(input, cancel);
        });
        preparation.submit(first);
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 3000);
        document.select(0);
        auto latest = document.planningInput();
        const auto expected = PlacedJobPlanning::build(latest);
        preparation.submit(latest);
        latest.document.selectedIds = {first.document.selectedIds.first()};
        latest.document.items[0].bounds.moveLeft(500);
        QTRY_COMPARE_WITH_TIMEOUT(preparation.state(), QString("ready"), 5000);
        QCOMPARE(rectangle(preparation.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(10, 20, 20, 10));
        QCOMPARE(preparation.placementSummary()["sourceFingerprint"],
                 expected.summary["sourceFingerprint"]);
        QCOMPARE(preparation.plan().encodedProgram().payload(),
                 expected.plan.encodedProgram().payload());
    }
    void selectionReferenceExclusionsAndMotionMetadata() {
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 50, 40);
        document.setLayerSetting(1, "speed", 777.);
        document.setLayerSetting(1, "power", 37.);
        document.setCutSelectedGraphics(true);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setPreviewPosition(position(document, {80, 70})));
        auto input = document.planningInput();
        input.document.items[0].bounds.moveLeft(500);
        auto result = PlacedJobPlanning::build(input);
        QVERIFY2(result.plan.valid(), qPrintable(result.plan.errors.join(";")));
        QCOMPARE(rectangle(result.summary["sourceOriginBounds"].toMap()), QRectF(50, 20, 470, 30));
        for (const auto &move : result.plan.moves)
            if (!move.rapid && move.power > 0) {
                QCOMPARE(move.speed, 777.);
                QCOMPARE(move.power, 37.);
                QCOMPARE(move.layer, 1);
            }
        // T1, T2, hidden, inactive page and disabled operations must not become origin geometry.
        for (int exclusion = 0; exclusion < 5; ++exclusion) {
            auto excluded = input;
            auto &art = excluded.document.items[0];
            if (exclusion < 2)
                art.toolLayer = exclusion + 1;
            if (exclusion == 2)
                art.visible = false;
            if (exclusion == 3)
                art.page = 1;
            if (exclusion == 4) {
                art.layer = 0;
                auto layer = excluded.document.layers[0].toMap();
                layer["enabled"] = false;
                excluded.document.layers[0] = layer;
            }
            const auto output = PlacedJobPlanning::build(excluded);
            QVERIFY(output.plan.valid());
            QCOMPARE(rectangle(output.summary["sourceOriginBounds"].toMap()),
                     QRectF(50, 40, 20, 10));
        }
        // Masked batch/material-test workflows must not silently export all objects.
        QVERIFY(document.setStartMode(Document::AbsoluteCoordinates));
        const auto csv = QUrl::fromLocalFile(storage.filePath("selected-batch.csv"));
        QFile file(csv.toLocalFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("name\nA\n");
        file.close();
        QTemporaryDir output;
        QVERIFY(!document.batch(csv, QUrl::fromLocalFile(output.path()), "{name}"));
        QVERIFY(QDir(output.path()).entryList(QDir::Files).isEmpty());
        const auto material =
            QUrl::fromLocalFile(storage.filePath("absolute-selected-material.gcode"));
        QVERIFY(!document.exportMaterialTest(material, 500, 1000, 10, 20, 2));
        QVERIFY(!QFile::exists(material.toLocalFile()));
    }
    void selectedOutputAndOriginAreIndependent_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("anchor");
        QTest::addColumn<bool>("selectionOrigin");
        for (int mode = 0; mode < 3; ++mode)
            for (int anchor = 0; anchor < 9; ++anchor)
                for (bool selected : {false, true})
                    QTest::newRow(qPrintable(QString("mode%1-anchor%2-selectionOrigin%3")
                                                 .arg(mode)
                                                 .arg(anchor)
                                                 .arg(selected)))
                        << mode << anchor << selected;
    }
    void selectedOutputAndOriginAreIndependent() {
        QFETCH(int, mode);
        QFETCH(int, anchor);
        QFETCH(bool, selectionOrigin);
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 50, 40);
        const auto editor = document.jobInput().items;
        const auto allPlan = document.job();
        document.setCutSelectedGraphics(true);
        document.setUseSelectionOrigin(selectionOrigin);
        QVERIFY(document.setJobOrigin(Document::JobOrigin(anchor)));
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setPreviewPosition(position(document, {110, 90}, 2)));
        QVERIFY(document.setStartMode(Document::StartMode(mode)));
        const QPointF target = mode == 1 ? QPointF(110, 90) : QPointF(100, 80);
        const QRectF reference = selectionOrigin ? QRectF(50, 40, 20, 10) : QRectF(10, 20, 60, 30);
        const QPointF delta =
            mode == 0 ? QPointF()
                      : target - QPointF(reference.x() + (anchor % 3) * reference.width() / 2,
                                         reference.y() + (anchor / 3) * reference.height() / 2);
        const QRectF expected = QRectF(50, 40, 20, 10).translated(delta);
        QVERIFY2(document.job().valid(), qPrintable(document.job().errors.join(";")));
        const auto summary = document.placementSummary();
        QCOMPARE(rectangle(summary["graphicsBounds"].toMap()), expected);
        QCOMPARE(rectangle(summary["sourceGraphicsBounds"].toMap()), QRectF(50, 40, 20, 10));
        QCOMPARE(rectangle(summary["designGraphicsBounds"].toMap()), QRectF(10, 20, 60, 30));
        QCOMPARE(summary["originScope"].toString(),
                 mode != 0 && selectionOrigin ? QString("selection") : QString("design"));
        QCOMPARE(summary["outputScope"].toString(), QString("selection"));
        int working = 0;
        for (const auto &move : document.job().moves) {
            if (!move.rapid && move.power > 0) {
                ++working;
                QVERIFY(expected.adjusted(-1e-8, -1e-8, 1e-8, 1e-8).contains(move.from));
                QVERIFY(expected.adjusted(-1e-8, -1e-8, 1e-8, 1e-8).contains(move.to));
            }
        }
        QCOMPARE(working, 4);
        QVERIFY(document.job().workLength < allPlan.workLength);
        const auto after = document.jobInput().items;
        QCOMPARE(after.size(), editor.size());
        for (int i = 0; i < after.size(); ++i)
            QCOMPARE(after[i].bounds, editor[i].bounds);
        const auto path = QUrl::fromLocalFile(storage.filePath("selected-output.gcode"));
        QVERIFY(document.exportGcode(path));
        QCOMPARE(read(path), document.job().encodedProgram().payload());
    }
    void selectionSnapshotIsBoundedAndDoesNotFallback() {
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 50, 40);
        auto input = document.jobInput();
        input.cutSelectedGraphics = true;
        input.selectedIds = {input.items[1].id};
        input.items[0].bounds.moveLeft(
            500); // Unselected out-of-bed graphics do not poison the output plan.
        auto plan = JobPlanner::build(input);
        QVERIFY2(plan.valid(), qPrintable(plan.errors.join(";")));
        input.selectedIds.clear();
        QVERIFY(!JobPlanner::build(input).valid());
        input.selectedIds = {"stale-id"};
        QVERIFY(!JobPlanner::build(input).valid());
        input.selectedIds = {input.items[1].id, input.items[1].id};
        QVERIFY(!JobPlanner::build(input).valid());
        input.selectedIds = {input.items[1].id};
        for (int exclusion = 0; exclusion < 4; ++exclusion) {
            auto excluded = input;
            if (exclusion == 0)
                excluded.items[1].visible = false;
            if (exclusion == 1)
                excluded.items[1].toolLayer = 1;
            if (exclusion == 2)
                excluded.items[1].page = 1;
            if (exclusion == 3) {
                auto layer = excluded.layers[excluded.items[1].layer].toMap();
                layer["enabled"] = false;
                excluded.layers[excluded.items[1].layer] = layer;
            }
            QVERIFY2(!JobPlanner::build(excluded).valid(), qPrintable(QString::number(exclusion)));
        }
        document.setCutSelectedGraphics(true);
        document.select(-1);
        QVERIFY(!document.job().valid());
        const auto output = QUrl::fromLocalFile(storage.filePath("empty-selection.gcode"));
        QVERIFY(!document.exportGcode(output));
        QVERIFY(!QFile::exists(output.toLocalFile()));
        auto mismatch = document.planningInput();
        mismatch.document.cutSelectedGraphics = false;
        QVERIFY(!PlacedJobPlanning::build(mismatch).plan.valid());
    }
    void selectionChangesInvalidateAsyncAndIdenticalBoundsFrames() {
        Document document;
        document.newProject();
        rectangle(document, 10, 20);
        rectangle(document, 10, 20);
        document.setCutSelectedGraphics(true);
        QVERIFY(document.save(QUrl::fromLocalFile(storage.filePath("selection-clean.blazer"))));
        const auto first = document.placementSummary()["sourceFingerprint"];
        const auto oldFrame = document.framingBounds("job");
        document.select(0);
        QVERIFY(!document.dirty());
        QVERIFY(document.job().valid());
        QVERIFY(document.placementSummary()["sourceFingerprint"] != first);
        const auto newFrame = document.framingBounds("job");
        QVERIFY(newFrame != oldFrame);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        machine.frame(oldFrame);
        QVERIFY(!machine.busy());
        QVERIFY(document.message().contains("veraltet"));
        document.setBackgroundPlanning(true);
        for (int i = 0; i < 20; ++i)
            document.select(i % 2);
        document.select(0);
        QTRY_COMPARE_WITH_TIMEOUT(document.planningState(), QString("ready"), 5000);
        QCOMPARE(document.job().motionFingerprint(),
                 PlacedJobPlanning::build(document.planningInput()).plan.motionFingerprint());
        document.select(-1);
        QTRY_COMPARE_WITH_TIMEOUT(document.planningState(), QString("failed"), 5000);
        QVERIFY(!document.job().valid());
        QVERIFY(!document.dirty());
    }
    void selectionIntentVersion14AndRuntimeProofStaySeparate() {
        Document document;
        document.newProject();
        rectangle(document);
        document.setCutSelectedGraphics(true);
        document.setUseSelectionOrigin(true);
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.job().valid());
        auto root = QJsonDocument::fromJson(document.projectBytes()).object();
        QCOMPARE(root["version"].toInt(), 14);
        auto intent = root["placement"].toObject();
        QCOMPARE(intent.size(), 6);
        QCOMPARE(intent["version"].toInt(), 2);
        QVERIFY(intent["cutSelectedGraphics"].toBool());
        QVERIFY(intent["useSelectionOrigin"].toBool());
        QVERIFY(!document.projectBytes().contains("coordinateFrame"));
        const auto file = QUrl::fromLocalFile(storage.filePath("selected14.blazer"));
        QVERIFY(document.save(file));
        Document reopened;
        QVERIFY(reopened.open(file));
        QVERIFY(reopened.cutSelectedGraphics());
        QVERIFY(reopened.useSelectionOrigin());
        QVERIFY(!reopened.hasPreviewPosition());
        QVERIFY(!reopened.job().valid());
        QVERIFY(reopened.setPreviewPosition(position(reopened, {100, 80})));
        QVERIFY(!reopened.job().valid());
        QVERIFY(reopened.confirmUserOrigin());
        QVERIFY(reopened.job().valid());
        for (int malformed = 0; malformed < 5; ++malformed) {
            auto invalid = root;
            auto placement = intent;
            if (malformed == 0)
                invalid["version"] = 13;
            if (malformed == 1)
                placement["version"] = 1;
            if (malformed == 2)
                placement.remove("cutSelectedGraphics");
            if (malformed == 3)
                placement["useSelectionOrigin"] = 1;
            if (malformed == 4)
                placement["unknown"] = true;
            invalid["placement"] = placement;
            QFile out(file.toLocalFile());
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(invalid).toJson());
            out.close();
            const auto before = reopened.projectBytes();
            QVERIFY(!reopened.open(file));
            QCOMPARE(reopened.projectBytes(), before);
        }
        document.setCutSelectedGraphics(false);
        document.setUseSelectionOrigin(false);
        QCOMPARE(QJsonDocument::fromJson(document.projectBytes()).object()["version"].toInt(), 13);
        document.setCutSelectedGraphics(true);
        const auto exportFile = QUrl::fromLocalFile(storage.filePath("selection-material.gcode"));
        QVERIFY(!document.exportMaterialTest(exportFile, 500, 1000, 10, 20, 2));
        QVERIFY(!QFile::exists(exportFile.toLocalFile()));
    }
    void defaultAbsoluteRetainsOldFormatAndRuntimePositionNeverDirtiesProject() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.save(QUrl::fromLocalFile(storage.filePath("default-clean.blazer"))));
        QVERIFY(!document.dirty());
        const auto bytes = document.projectBytes();
        QCOMPARE(QJsonDocument::fromJson(bytes).object()["version"].toInt(), 8);
        const auto canonical = JobPlanner::build(document.jobInput());
        QCOMPARE(document.job().gcode(), canonical.gcode());
        const auto history = document.historyRevision();
        const auto wasDirty = document.dirty();
        QVERIFY(document.setPreviewPosition(position(document, {150, 140})));
        QCOMPARE(document.projectBytes(), bytes);
        QCOMPARE(document.historyRevision(), history);
        QCOMPARE(document.dirty(), wasDirty);
        QCOMPARE(document.job().moves.first().from, QPointF(150, 140));
        QVERIFY(document.placementSummary()["approachKnown"].toBool());
        document.clearPreviewPosition();
        QCOMPARE(document.job().gcode(), canonical.gcode());
        QVERIFY(!document.placementSummary()["approachKnown"].toBool());
    }
    void identicalRuntimeSnapshotsDoNotReplanAndIntentUsesExactCoordinates() {
        Document document;
        document.newProject();
        rectangle(document);
        const auto snapshot = position(document, {50, 50});
        QVERIFY(document.setPreviewPosition(snapshot));
        QVERIFY(document.hasPreviewPosition());
        QSignalSpy changes(&document, &Document::changed);
        QVERIFY(document.setPreviewPosition(snapshot));
        QCOMPARE(changes.count(), 0);
        document.clearPreviewPosition();
        changes.clear();
        document.clearPreviewPosition();
        QCOMPARE(changes.count(), 0);
        QVERIFY(!document.hasPreviewPosition());
        QVERIFY(document.setPreviewPosition(snapshot));
        QVERIFY(document.captureUserOrigin());
        const auto file = QUrl::fromLocalFile(storage.filePath("exact-intent.blazer"));
        QVERIFY(document.save(file));
        const auto before = document.projectBytes();
        const auto revision = document.historyRevision();
        QVERIFY(document.setPreviewPosition(position(document, {std::nextafter(50., 51.), 50}, 2)));
        QVERIFY(!document.dirty());
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.dirty());
        QVERIFY(document.projectBytes() != before);
        QVERIFY(document.historyRevision() > revision);
    }
    void currentModeUsesOnlyOutputArtworkBoundsAndExportsExactlyPreview() {
        Document document;
        document.newProject();
        rectangle(document);
        rectangle(document, 140, 100);
        document.toggleVisible(1);
        rectangle(document, 140, 100);
        QVERIFY(document.setToolLayer(1));
        rectangle(document, 140, 100);
        document.setLayer(0);
        document.setLayerSetting(0, "enabled", false);
        rectangle(document, 140, 100);
        document.setObjectSetting("power", 0.);
        QVERIFY(document.addPage("Other page"));
        document.activatePage(1);
        rectangle(document, 140, 100);
        document.activatePage(0);
        const auto originalItems = document.items();
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(!document.job().valid());
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        QVERIFY2(document.job().valid(), qPrintable(document.job().errors.join(";")));
        QCOMPARE(document.items(), originalItems);
        auto summary = document.placementSummary();
        QCOMPARE(rectangle(summary["sourceGraphicsBounds"].toMap()), QRectF(10, 20, 20, 10));
        QCOMPARE(rectangle(summary["graphicsBounds"].toMap()), QRectF(90, 75, 20, 10));
        QCOMPARE(document.job().moves.first().from, QPointF(100, 80));
        QCOMPARE(document.job().moves.first().to, QPointF(90, 75));
        QCOMPARE(JobPlanner::build(document).motionFingerprint(),
                 document.job().motionFingerprint());
        const auto output = QUrl::fromLocalFile(storage.filePath("placed.gcode"));
        QVERIFY(document.exportGcode(output));
        QCOMPARE(read(output), document.job().encodedProgram().payload());
        QVERIFY(read(output).contains("G0 X90.000 Y75.000"));
        const auto savedCurrent = QUrl::fromLocalFile(storage.filePath("current-intent.blazer"));
        QVERIFY(document.save(savedCurrent));
        Document reopened;
        QVERIFY(reopened.open(savedCurrent));
        QCOMPARE(reopened.startMode(), Document::CurrentPosition);
        QCOMPARE(reopened.jobOrigin(), Document::Center);
        QVERIFY(!reopened.job().valid());
        QVERIFY(reopened.setPreviewPosition(position(reopened, {100, 80})));
        QCOMPARE(reopened.job().gcode(), document.job().gcode());
        const auto signature = summary["sourceFingerprint"].toString();
        QVERIFY(signature.size() == 64);
        document.select(0);
        document.setObjectSetting("power", 45.);
        QVERIFY(document.placementSummary()["sourceFingerprint"].toString() != signature);
    }
    void overscanIsValidatedButDoesNotShiftGraphicsAnchor_data() {
        QTest::addColumn<bool>("masked");
        QTest::addColumn<bool>("selectionOrigin");
        QTest::newRow("whole-design") << false << false;
        QTest::newRow("selected-design-origin") << true << false;
        QTest::newRow("selected-selection-origin") << true << true;
    }
    void overscanIsValidatedButDoesNotShiftGraphicsAnchor() {
        QFETCH(bool, masked);
        QFETCH(bool, selectionOrigin);
        Document document;
        document.newProject();
        rectangle(document);
        document.setLayer(0);
        document.setLayerSetting(0, "overscan", 2.);
        document.setLayerSetting(0, "interval", 1.);
        document.setCutSelectedGraphics(masked);
        document.setUseSelectionOrigin(selectionOrigin);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setPreviewPosition(position(document, {50, 50})));
        QVERIFY2(document.job().valid(), qPrintable(document.job().errors.join(";")));
        QCOMPARE(rectangle(document.placementSummary()["sourceGraphicsBounds"].toMap()),
                 QRectF(10, 20, 20, 10));
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(50, 50, 20, 10));
        bool overscan = false;
        for (const auto &move : document.job().moves)
            overscan |= !move.rapid && move.power == 0 && (move.from.x() < 50 || move.to.x() > 70);
        QVERIFY(overscan);
        QVERIFY(document.setPreviewPosition(position(document, {0, 50}, 2)));
        QVERIFY(!document.job().valid());
        const auto path = QUrl::fromLocalFile(storage.filePath("overscan-invalid.gcode"));
        QVERIFY(!document.exportGcode(path));
        QVERIFY(!QFile::exists(path.toLocalFile()));
    }
    void strictVersion13RoundtripHasIntentOnlyAndRejectsMalformedAtomically() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.setPreviewPosition(position(document, {50, 50})));
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.setJobOrigin(Document::UpperRight));
        const auto bytes = document.projectBytes();
        const auto root = QJsonDocument::fromJson(bytes).object();
        QCOMPARE(root["version"].toInt(), 13);
        QVERIFY(root["deviceProfile"].isObject());
        QVERIFY(root["guides"].isArray());
        for (const auto &item : root["objects"].toArray()) {
            QVERIFY(item.toObject()["toolLayer"].isDouble());
            QVERIFY(item.toObject()["engraveOutline"].isBool());
        }
        const auto serializedPlacement = root["placement"].toObject();
        QCOMPARE(serializedPlacement.size(), 4);
        QVERIFY(!bytes.contains("coordinateFrameFingerprint"));
        QVERIFY(!bytes.contains("confirmedOrigin"));
        QVERIFY(!bytes.contains("startPosition"));
        QVERIFY(!bytes.contains("known"));
        const auto savedFile = QUrl::fromLocalFile(storage.filePath("placement-intent.blazer"));
        QVERIFY(document.save(savedFile));
        Document restored;
        QVERIFY(restored.open(savedFile));
        QCOMPARE(restored.startMode(), Document::UserOrigin);
        QCOMPARE(restored.jobOrigin(), Document::UpperRight);
        QVERIFY(!restored.job().valid());
        QVERIFY(!restored.placementSummary()["originConfirmed"].toBool());
        QVERIFY(restored.setPreviewPosition(position(restored, {120, 100})));
        QVERIFY(!restored.job().valid());
        QVERIFY(restored.confirmUserOrigin());
        QVERIFY(restored.job().valid());
        QCOMPARE(rectangle(restored.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(30, 40, 20, 10));
        QCOMPARE(restored.job().moves.first().from, QPointF(120, 100));
        const auto unchanged = restored.projectBytes();
        QList<QJsonObject> malformed;
        auto bad = root;
        bad.remove("placement");
        malformed.append(bad);
        bad = root;
        auto p = serializedPlacement;
        p["mode"] = true;
        bad["placement"] = p;
        malformed.append(bad);
        p = serializedPlacement;
        p["anchor"] = 9;
        bad["placement"] = p;
        malformed.append(bad);
        p = serializedPlacement;
        p["known"] = true;
        bad["placement"] = p;
        malformed.append(bad);
        p = serializedPlacement;
        p["userOrigin"] = QJsonObject{{"x", 50}, {"y", 50}, {"frame", "injected"}};
        bad["placement"] = p;
        malformed.append(bad);
        bad = root;
        bad["version"] = 12;
        malformed.append(bad);
        bad = root;
        bad.remove("deviceProfile");
        malformed.append(bad);
        for (const auto &json : malformed) {
            QVERIFY(!restored.restoreBytes(QJsonDocument(json).toJson()));
            QCOMPARE(restored.projectBytes(), unchanged);
            QVERIFY(restored.job().valid());
            QCOMPARE(restored.fileUrl(), savedFile);
        }
    }
    void undoRestoresIntentWithoutRestoringProofAndNewFrameRequiresConfirmation() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.setPreviewPosition(position(document, {50, 50})));
        QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setStartMode(Document::UserOrigin));
        QVERIFY(document.job().valid());
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(document.job().valid());
        document.undo();
        QCOMPARE(document.jobOrigin(), Document::LowerLeft);
        QVERIFY(!document.job().valid());
        QVERIFY(document.confirmUserOrigin());
        QVERIFY(document.job().valid());
        const auto intent = document.projectBytes();
        QVERIFY(document.setPreviewPosition(position(document, {100, 80}, 2, QByteArray(64, 'c'))));
        QVERIFY(!document.job().valid());
        QCOMPARE(document.projectBytes(), intent);
        QVERIFY(document.confirmUserOrigin());
        QVERIFY(document.job().valid());
        auto invalid = position(document, {0, 0});
        invalid.known = false;
        QVERIFY(!document.setPreviewPosition(invalid));
        QVERIFY(!document.job().valid());
        QVERIFY(!document.placementSummary()["previewPositionKnown"].toBool());
        const auto batchDir = QUrl::fromLocalFile(storage.filePath("not-created"));
        QVERIFY(!document.batch({}, batchDir, {}));
        QVERIFY(!QFile::exists(batchDir.toLocalFile()));
        const auto testFile = QUrl::fromLocalFile(storage.filePath("material-not-created.gcode"));
        QVERIFY(!document.exportMaterialTest(testFile, 1000, 2000, 10, 20, 2));
        QVERIFY(!QFile::exists(testFile.toLocalFile()));
    }
    void backgroundPositionUpdatesInvalidateOldPlansAndExportOnlyLatest() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        document.setBackgroundPlanning(true);
        QTRY_COMPARE_WITH_TIMEOUT(document.planningState(), QString("ready"), 5000);
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(90, 75, 20, 10));
        const auto previous = document.placementSummary()["requestFingerprint"].toString();
        QVERIFY(document.setPreviewPosition(position(document, {120, 90}, 2)));
        QVERIFY(!document.job().valid());
        const auto premature = QUrl::fromLocalFile(storage.filePath("queued-not-exported.gcode"));
        QVERIFY(!document.exportGcode(premature));
        QVERIFY(!QFile::exists(premature.toLocalFile()));
        QTRY_COMPARE_WITH_TIMEOUT(document.planningState(), QString("ready"), 5000);
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(110, 85, 20, 10));
        QVERIFY(document.placementSummary()["requestFingerprint"].toString() != previous);
        const auto output = QUrl::fromLocalFile(storage.filePath("async-placed.gcode"));
        QVERIFY(document.exportGcode(output));
        QCOMPARE(read(output), document.job().encodedProgram().payload());
        document.cancelPlanning();
        QVERIFY(!document.job().valid());
        document.retryPlanning();
        QTRY_COMPARE_WITH_TIMEOUT(document.planningState(), QString("ready"), 5000);
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(110, 85, 20, 10));
    }
    void pageChangesPreservePlacementContext_data() {
        QTest::addColumn<bool>("background");
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("anchor");
        QTest::addColumn<bool>("confirmed");
        for (bool background : {false, true})
            for (int anchor : {0, 4, 8})
                for (int kind : {0, 1, 2}) {
                    const auto name = QString("%1-anchor%2-%3")
                                          .arg(background ? "async" : "sync")
                                          .arg(anchor)
                                          .arg(kind == 0   ? "current"
                                               : kind == 1 ? "user-confirmed"
                                                           : "user-unconfirmed");
                    QTest::newRow(name.toLatin1().constData())
                        << background << (kind == 0 ? 1 : 2) << anchor << (kind != 2);
                }
    }
    void pageChangesPreservePlacementContext() {
        QFETCH(bool, background);
        QFETCH(int, mode);
        QFETCH(int, anchor);
        QFETCH(bool, confirmed);
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.addPage("Second"));
        document.activatePage(1);
        document.addShape("ellipse");
        document.setLayer(1);
        document.setProperty("x", 10.);
        document.setProperty("y", 20.);
        document.setProperty("width", 20.);
        document.setProperty("height", 10.);
        document.activatePage(0);
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        if (mode == 2)
            QVERIFY(document.captureUserOrigin());
        QVERIFY(document.setStartMode(Document::StartMode(mode)));
        QVERIFY(document.setJobOrigin(Document::JobOrigin(anchor)));
        if (!confirmed) {
            const auto file =
                QUrl::fromLocalFile(storage.filePath(QString(QTest::currentDataTag()) + ".blazer"));
            QVERIFY(document.save(file));
            QVERIFY(document.open(file));
        }
        document.setBackgroundPlanning(background);
        auto ready = [&](bool valid) {
            if (background)
                QTRY_COMPARE_WITH_TIMEOUT(document.planningState(),
                                          valid ? QString("ready") : QString("failed"), 5000);
            QCOMPARE(document.job().valid(), valid);
        };
        ready(confirmed);
        const auto before = document.framingBounds("job");
        document.activatePage(1);
        ready(confirmed);
        const auto output =
            QUrl::fromLocalFile(storage.filePath(QString(QTest::currentDataTag()) + ".gcode"));
        if (!confirmed) {
            QVERIFY(!document.exportGcode(output));
            QVERIFY(!QFile::exists(output.toLocalFile()));
            QVERIFY(!document.placementSummary()["originConfirmed"].toBool());
            QVERIFY(document.confirmUserOrigin());
            ready(true);
        }
        const QRectF expected = anchor == 0   ? QRectF(100, 80, 20, 10)
                                : anchor == 4 ? QRectF(90, 75, 20, 10)
                                              : QRectF(80, 70, 20, 10);
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()), expected);
        if (confirmed)
            QVERIFY(document.framingBounds("job")["sourceRevision"] != before["sourceRevision"]);
        const auto sync = PlacedJobPlanning::build(document.planningInput()).plan;
        QVERIFY(sync.valid());
        QCOMPARE(document.job().motionFingerprint(), sync.motionFingerprint());
        QVERIFY(document.exportGcode(output));
        QCOMPARE(read(output), sync.encodedProgram().payload());
        document.activatePage(0);
        ready(true);
        QCOMPARE(rectangle(document.placementSummary()["graphicsBounds"].toMap()), expected);
        QCOMPARE(document.job().motionFingerprint(),
                 PlacedJobPlanning::build(document.planningInput()).plan.motionFingerprint());
    }
    void pageChangesInvalidateIdenticalBoundsFrameTargets() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.addPage("Second"));
        document.activatePage(1);
        rectangle(document);
        document.activatePage(0);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        const auto original = document.framingBounds("job");
        QVERIFY(original["valid"].toBool());
        document.activatePage(1);
        QCOMPARE(rectangle(document.framingBounds("job")), rectangle(original));
        machine.frame(original);
        QVERIFY(!machine.busy());
        QVERIFY(document.message().contains("veraltet"));
    }
    void slowBackgroundSourceCannotPublishOldPositionAfterResubmit() {
        Document document;
        document.newProject();
        rectangle(document);
        QVERIFY(document.setStartMode(Document::CurrentPosition));
        QVERIFY(document.setJobOrigin(Document::Center));
        QVERIFY(document.setPreviewPosition(position(document, {100, 80})));
        auto first = document.planningInput();
        std::atomic_bool started = false;
        std::atomic_int calls = 0;
        JobPreparation preparation(nullptr, [&](const JobInput &input, const Cancellation &cancel) {
            if (++calls == 1) {
                started = true;
                while (!isCancelled(cancel))
                    QThread::msleep(1);
            }
            return JobPlanner::build(input, cancel);
        });
        preparation.submit(first);
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 3000);
        auto latest = first;
        latest.placement.startPosition = position(document, {120, 90}, 2);
        preparation.submit(latest);
        latest.placement.startPosition->point = {0, 0};
        QTRY_COMPARE_WITH_TIMEOUT(preparation.state(), QString("ready"), 5000);
        QCOMPARE(rectangle(preparation.placementSummary()["graphicsBounds"].toMap()),
                 QRectF(110, 85, 20, 10));
        QCOMPARE(preparation.plan().moves.first().from, QPointF(120, 90));
        QVERIFY(!preparation.plan().cachedCommands().isEmpty());
    }
};
QTEST_MAIN(PlacementDocumentTest)
#include "placementdocument_test.moc"
