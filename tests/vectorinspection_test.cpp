#include "document.h"
#include "vectorinspection.h"
#include "vectorinspectionservice.h"
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest>
class VectorInspectionTest : public QObject {
    Q_OBJECT
    JobInput input() {
        JobInput result;
        result.layers = {
            QVariantMap{{"mode", "Gravur"}, {"enabled", true}, {"power", 20}, {"passes", 1}},
            QVariantMap{{"mode", "Schnitt"}, {"enabled", true}, {"power", 40}, {"passes", 3}}};
        return result;
    }
    Artwork item(QString id, QPainterPath path, int layer = 1) {
        Artwork value;
        value.id = id;
        value.name = "Name does not establish geometry identity";
        value.kind = "path";
        value.layer = layer;
        value.path = path;
        value.bounds = {10, 10, 20, 20};
        return value;
    }
    QPainterPath square(bool reversed = false) {
        QPainterPath path;
        path.moveTo(0, 0);
        path.lineTo(reversed ? QPointF(0, 1) : QPointF(1, 0));
        path.lineTo(1, 1);
        path.lineTo(reversed ? QPointF(1, 0) : QPointF(0, 1));
        path.closeSubpath();
        return path;
    }
    int count(const VectorInspection::Report &report, const QString &code) {
        return std::count_if(report.findings.begin(), report.findings.end(),
                             [&](const auto &finding) { return finding.code == code; });
    }
  private slots:
    void duplicatesIgnoreIdsNamesDirectionStartingPointAndConfiguredPasses() {
        auto snapshot = input();
        snapshot.items = {item("a", square()), item("b", square(true))};
        snapshot.items[1].name = "Entirely different";
        auto report = VectorInspection::inspect(snapshot);
        QVERIFY(report.complete);
        QCOMPARE(count(report, "duplicate-cut-contour"), 1);
        QCOMPARE(report.findings.first().objectIds, QStringList({"a", "b"}));
        snapshot.items.removeLast();
        report = VectorInspection::inspect(snapshot);
        QVERIFY(report.findings.isEmpty()); // Three requested passes are intentional.
        snapshot.items[0].path.addPath(square());
        report = VectorInspection::inspect(snapshot);
        QCOMPARE(count(report, "duplicate-cut-contour"), 1);
    }
    void openCutAndDeliberateMarkingAndExtraCutAreDistinct() {
        QPainterPath line;
        line.moveTo(0, 0);
        line.lineTo(1, 1);
        auto snapshot = input();
        snapshot.items = {item("mark", line, 0)};
        snapshot.items[0].engraveOutline = true;
        auto report = VectorInspection::inspect(snapshot);
        QVERIFY(report.findings.isEmpty());
        snapshot.items[0].extraOperations = {1};
        report = VectorInspection::inspect(snapshot);
        QCOMPARE(count(report, "open-cut-contour"), 1);
        snapshot.items[0].extraOperations.clear();
        snapshot.items[0].layer = 1;
        report = VectorInspection::inspect(snapshot);
        QCOMPARE(count(report, "open-cut-contour"), 1);
    }
    void bowtieSelfIntersectionReportsActualMillimetres() {
        QPainterPath bow;
        bow.moveTo(0, 0);
        bow.lineTo(1, 1);
        bow.lineTo(0, 1);
        bow.lineTo(1, 0);
        bow.closeSubpath();
        auto snapshot = input();
        snapshot.items = {item("bow", bow)};
        const auto report = VectorInspection::inspect(snapshot);
        QCOMPARE(count(report, "self-intersection"), 1);
        const auto finding = *std::find_if(report.findings.begin(), report.findings.end(),
                                           [](auto f) { return f.code == "self-intersection"; });
        QCOMPARE(finding.positionMm, QPointF(20, 20));
        QPainterPath separate;
        separate.moveTo(0, 0);
        separate.lineTo(1, 1);
        separate.moveTo(0, 1);
        separate.lineTo(1, 0);
        snapshot.items = {item("crossing-subpaths", separate, 0)};
        QCOMPARE(count(VectorInspection::inspect(snapshot), "self-intersection"), 1);
    }
    void legitimateNestedContoursDisjointSubpathsAndQuietZonesRemainQuiet() {
        QPainterPath paths;
        paths.addRect({0, 0, 1, 1});
        paths.addRect({.2, .2, .3, .3});
        paths.addRect({1.5, 0, .2, .2});
        auto snapshot = input();
        snapshot.items = {item("nested", paths)};
        Artwork quiet;
        quiet.id = "quiet-zone";
        quiet.kind = "rect";
        quiet.bounds = {-20, -20, 400, 400};
        quiet.layer = 1;
        quiet.toolLayer = 1;
        snapshot.items.append(quiet);
        const auto report = VectorInspection::inspect(snapshot);
        QVERIFY(report.complete);
        QVERIFY(report.findings.isEmpty());
        QCOMPARE(report.inspectedObjects, 1);
    }
    void svgTransformsUseExistingActualOutputGeometry() {
        QString error;
        const auto svg =
            SvgAsset::parse("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='20mm' "
                            "viewBox='0 0 20 20'><g transform='translate(2 3)'><path d='M0 0L10 "
                            "10L0 10L10 0Z' fill='none' stroke='black'/></g></svg>",
                            error);
        QVERIFY2(svg, qPrintable(error));
        auto snapshot = input();
        Artwork art;
        art.id = "svg";
        art.kind = "svg";
        art.asset = svg;
        art.bounds = {50, 60, 20, 20};
        art.layer = 1;
        snapshot.items = {art};
        const auto report = VectorInspection::inspect(snapshot);
        QCOMPARE(count(report, "self-intersection"), 1);
        const auto found = *std::find_if(report.findings.begin(), report.findings.end(),
                                         [](auto f) { return f.code == "self-intersection"; });
        QVERIFY(QLineF(found.positionMm, QPointF(57, 72)).length() < .05);
    }
    void adjacentCornersStayQuietButBacktrackingAndPairBudgetAreExplicit() {
        auto snapshot = input();
        QPainterPath backtrack;
        backtrack.moveTo(0, 0);
        backtrack.lineTo(1, 0);
        backtrack.lineTo(0, 0);
        snapshot.items = {item("backtrack", backtrack)};
        QCOMPARE(count(VectorInspection::inspect(snapshot), "self-intersection"), 1);
        QPainterPath many;
        many.moveTo(0, 0);
        for (int i = 1; i < 1500; ++i)
            many.lineTo(i / 1500., (i % 2) / 10.);
        snapshot.items = {item("pair-budget", many, 0)};
        auto report = VectorInspection::inspect(snapshot);
        QVERIFY(!report.complete);
        QCOMPARE(count(report, "work-limit"), 1);
        QVERIFY(report.pairChecks <= 500000);
    }
    void outsideDegenerateSelectionCancellationAndPathologicalInputAreExplicit() {
        auto snapshot = input();
        auto art = item("outside", square());
        art.bounds = {195, 145, 20, 20};
        snapshot.items = {art};
        QCOMPARE(count(VectorInspection::inspect(snapshot), "outside-work-area"), 1);
        QVERIFY(VectorInspection::inspect(snapshot, {"other"}).findings.isEmpty());
        snapshot.items = {item("empty", {})};
        QCOMPARE(count(VectorInspection::inspect(snapshot), "degenerate-contour"), 1);
        auto flag = std::make_shared<std::atomic_bool>(true);
        auto report = VectorInspection::inspect(snapshot, {}, flag);
        QVERIFY(report.cancelled);
        QVERIFY(!report.complete);
        QPainterPath enormous;
        enormous.moveTo(0, 0);
        for (int i = 0; i < 5000; ++i)
            enormous.lineTo(i % 2, (i % 3) / 3.);
        snapshot.items = {item("pathological", enormous)};
        report = VectorInspection::inspect(snapshot);
        QVERIFY(!report.complete);
        QCOMPARE(count(report, "work-limit"), 1);
        QVERIFY(report.toVariantMap()["advisoryOnly"].toBool());
    }
    void serviceIgnoresGenericNotificationsButRejectsChangedSnapshotsAndCancel() {
        Document doc;
        doc.setBackgroundPlanning(false);
        doc.newProject();
        doc.addShape("rect");
        QSemaphore started, release;
        VectorInspectionService service(
            &doc, nullptr,
            [&](const JobInput &snapshot, const QStringList &ids, const Cancellation &cancel) {
                started.release();
                release.acquire();
                return VectorInspection::inspect(snapshot, ids, cancel);
            });
        QVERIFY(service.inspect());
        QVERIFY(started.tryAcquire(1, 2000));
        doc.notify("unrelated planning/status text");
        release.release();
        QTRY_VERIFY(!service.busy());
        QVERIFY(!service.report().isEmpty());
        QVERIFY(service.inspect());
        QVERIFY(started.tryAcquire(1, 2000));
        doc.setProperty("x", 40.);
        release.release();
        QTRY_VERIFY(!service.busy());
        QVERIFY(service.report().isEmpty());
        QVERIFY(service.inspect());
        QVERIFY(started.tryAcquire(1, 2000));
        service.cancel();
        release.release();
        QTRY_VERIFY(!service.busy());
        QVERIFY(service.report().isEmpty());
    }
    void serviceRequiresSelectionAndInvalidatesPublishedReportsOnEdit() {
        Document doc;
        doc.setBackgroundPlanning(false);
        doc.newProject();
        VectorInspectionService service(&doc);
        QVERIFY(!service.inspect(true));
        doc.addShape("rect");
        QVERIFY(service.inspect(true));
        QTRY_VERIFY(!service.busy());
        QVERIFY(!service.report().isEmpty());
        doc.setProperty("x", 50.);
        QVERIFY(service.report().isEmpty());
    }
};
QTEST_MAIN(VectorInspectionTest)
#include "vectorinspection_test.moc"
