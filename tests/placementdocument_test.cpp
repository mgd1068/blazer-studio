#include "document.h"
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
    void overscanIsValidatedButDoesNotShiftGraphicsAnchor() {
        Document document;
        document.newProject();
        rectangle(document);
        document.setLayer(0);
        document.setLayerSetting(0, "overscan", 2.);
        document.setLayerSetting(0, "interval", 1.);
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
