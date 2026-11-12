#include "document.h"
#include "imagelimits.h"
#include "vectorutil.h"
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto mime = "application/x-blazer-objects-v1";
}
class ClipboardTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    void put(const QByteArray &format, const QByteArray &bytes) {
        auto *data = new QMimeData;
        data->setData(format, bytes);
        QGuiApplication::clipboard()->setMimeData(data);
    }
    void text(const QString &value) {
        auto *data = new QMimeData;
        data->setText(value);
        QGuiApplication::clipboard()->setMimeData(data);
    }
  private slots:
    void initTestCase() { QVERIFY(QGuiApplication::platformName() == "offscreen"); }
    void nativeGroupsOperationsAssetsAndMetadata() {
        Document source;
        source.newProject();
        source.addShape("rect");
        source.setProperty("x", 20.);
        source.setProperty("y", 30.);
        const int custom = source.addOperation("Kontur extra", "Schnitt");
        QVERIFY(custom >= 2);
        source.assignOperations({1, custom});
        source.addShape("text");
        source.setProperty("text", "Test");
        source.toggleSelection(0);
        source.groupSelection();
        QVERIFY(source.setMetadata("Privater Titel", "Nicht kopieren"));
        QVERIFY(!source.addGuide("vertical",42.).isEmpty());
        const auto before = source.projectBytes();
        const auto revision = source.historyRevision();
        QVERIFY(source.copySelectionToClipboard());
        QCOMPARE(source.projectBytes(), before);
        QCOMPARE(source.historyRevision(), revision);
        const auto *data = QGuiApplication::clipboard()->mimeData();
        QVERIFY(data->hasFormat(mime));
        QVERIFY(data->hasFormat("image/svg+xml"));
        const auto encoded = data->data(mime);
        const auto root = QJsonDocument::fromJson(encoded).object();
        QVERIFY(!root.contains("guides"));
        QVERIFY(!root.contains("guidesVisible"));
        QCOMPARE(root["title"].toString(), QString("Zwischenablage"));
        QVERIFY(root["notes"].toString().isEmpty());
        QVERIFY(root["material"].toObject().isEmpty());
        Document target;
        target.newProject();
        target.setLayerSetting(1, "power", 15.);
        QVERIFY(target.addPage("Ziel"));
        QVERIFY(target.setMetadata("Eigenes Ziel", "Eigene Notizen"));
        const auto own = QUrl::fromLocalFile(temp.filePath("target.blazer"));
        QVERIFY(target.save(own));
        const auto targetBefore = target.projectBytes();
        const auto material = target.material();
        QVERIFY(target.pasteFromClipboard(true));
        QCOMPARE(target.count(), 2);
        QCOMPARE(target.items()[0].bounds, source.items()[0].bounds);
        QCOMPARE(target.items()[0].page, 1);
        QVERIFY(target.items()[0].id != source.items()[0].id);
        QVERIFY(!target.items()[0].groupId.isEmpty());
        QCOMPARE(target.items()[0].groupId, target.items()[1].groupId);
        QVERIFY(target.items()[0].groupId != source.items()[0].groupId);
        QCOMPARE(target.layers()[1].toMap()["power"].toDouble(), 15.);
        QCOMPARE(target.material(), material);
        QCOMPARE(target.fileUrl(), own);
        QCOMPARE(target.title(), QString("Eigenes Ziel"));
        QCOMPARE(target.notes(), QString("Eigene Notizen"));
        const auto &a = target.items()[0];
        QCOMPARE(target.layers()[a.layer], source.layers()[source.items()[0].layer]);
        QCOMPARE(target.layers()[a.extraOperations[0]],
                 source.layers()[source.items()[0].extraOperations[0]]);
        const auto layers = target.layers().size();
        const auto pasted = target.projectBytes();
        target.undo();
        QCOMPARE(target.projectBytes(), targetBefore);
        target.redo();
        QCOMPARE(target.projectBytes(), pasted);
        QVERIFY(target.pasteFromClipboard(true));
        QCOMPARE(target.layers().size(), layers);
        QVERIFY(target.items()[2].groupId != target.items()[0].groupId);
    }
    void svgFallbackIncludesSeparatedFlatPaths() {
        Document source;
        source.newProject();
        source.addShape("rect");
        source.addShape("rect");
        auto root = QJsonDocument::fromJson(source.projectBytes()).object();
        auto objects = root["objects"].toArray();
        QPainterPath line(QPointF(0, .5));
        line.lineTo(1, .5);
        for (int i = 0; i < 2; ++i) {
            auto a = objects[i].toObject();
            a["kind"] = "path";
            a["path"] = VectorUtil::encode(line);
            a["x"] = i ? 40 : 5;
            a["y"] = i ? 100 : 6;
            a["width"] = i ? 10 : 20;
            a["height"] = .1;
            objects[i] = a;
        }
        root["objects"] = objects;
        QVERIFY(source.restoreBytes(QJsonDocument(root).toJson()));
        source.select(0);
        source.toggleSelection(1);
        QVERIFY(source.copySelectionToClipboard());
        QString error;
        auto svg =
            SvgAsset::parse(QGuiApplication::clipboard()->mimeData()->data("image/svg+xml"), error);
        QVERIFY2(svg, qPrintable(error));
        QVERIFY(qAbs(svg->sizeMm.width() - 45.1) < 1e-8);
        QVERIFY(qAbs(svg->sizeMm.height() - 94.1) < 1e-8);
    }
    void imageFallbackAndNativeRoundtripPreserveCorrections() {
        QImage image(4, 3, QImage::Format_ARGB32);
        image.fill(Qt::gray);
        image.setPixelColor(0, 0, Qt::transparent);
        const auto file = temp.filePath("source.png");
        QVERIFY(image.save(file));
        Document source;
        source.newProject();
        QVERIFY(source.importImage(QUrl::fromLocalFile(file)));
        QVERIFY(source.command("imageSettings", {{"invert", true}, {"imageMode", "grayscale"}}));
        QVERIFY(source.copySelectionToClipboard());
        const auto *data = QGuiApplication::clipboard()->mimeData();
        QVERIFY(data->hasImage());
        QVERIFY(data->hasFormat("image/png"));
        Document target;
        target.newProject();
        QVERIFY(target.pasteFromClipboard(true));
        QCOMPARE(target.items()[0].style, source.items()[0].style);
        QCOMPARE(target.items()[0].reliefAsset->png, source.items()[0].reliefAsset->png);
        const auto code = target.job().gcode();
        Document restored;
        QVERIFY(restored.restoreBytes(target.projectBytes()));
        QCOMPARE(restored.job().gcode(), code);
        auto *external = new QMimeData;
        external->setImageData(image);
        QGuiApplication::clipboard()->setMimeData(external);
        target.newProject();
        QVERIFY(target.pasteFromClipboard());
        QCOMPARE(target.items()[0].kind, QString("image"));
        QCOMPARE(target.items()[0].bounds, QRectF(10, 10, .4, .3));
        QCOMPARE(target.items()[0].reliefAsset->image.pixelColor(0, 0).alpha(), 0);
    }
    void offsetsCutAndPartialGroups() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", 10.);
        d.setProperty("y", 10.);
        const auto bounds = d.items()[0].bounds;
        QVERIFY(d.copySelectionToClipboard());
        QVERIFY(d.pasteFromClipboard());
        QCOMPARE(d.items()[1].bounds, bounds.translated(5, 5));
        QVERIFY(d.pasteFromClipboard());
        QCOMPARE(d.items()[2].bounds, bounds.translated(10, 10));
        d.newProject();
        QVERIFY(d.pasteFromClipboard());
        QCOMPARE(d.items()[0].bounds, bounds.translated(5, 5));
        const auto before = d.projectBytes();
        QVERIFY(d.copySelectionToClipboard(true));
        QCOMPARE(d.count(), 0);
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        d.addShape("ellipse");
        d.toggleSelection(0);
        d.groupSelection();
        d.select(0);
        QVERIFY(d.copySelectionToClipboard());
        Document target;
        target.newProject();
        QVERIFY(target.pasteFromClipboard(true));
        QVERIFY(target.items()[0].groupId.isEmpty());
    }
    void externalSvgTextAndCorruptionAreAtomic() {
        Document d;
        d.newProject();
        put("image/svg+xml", "<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='10mm' "
                             "viewBox='0 0 20 10'><path d='M0 0L20 10'/></svg>");
        QVERIFY(d.pasteFromClipboard());
        QCOMPARE(d.items()[0].kind, QString("svg"));
        QCOMPARE(d.items()[0].bounds.size(), QSizeF(20, 10));
        text("<b>Nur Text</b>\nZeile zwei");
        QVERIFY(d.pasteFromClipboard());
        QCOMPARE(d.items()[1].kind, QString("text"));
        QCOMPARE(d.items()[1].text, QString("<b>Nur Text</b>\nZeile zwei"));
        const auto before = d.projectBytes();
        const auto revision = d.historyRevision();
        QSignalSpy resets(&d, &QAbstractItemModel::modelReset);
        for (const auto &bad : QList<QPair<QByteArray, QByteArray>>{
                 {mime, "{}"},
                 {mime, QByteArray(ImageLimits::projectBytes + 1, ' ')},
                 {"image/svg+xml", ""},
                 {"image/svg+xml", "<!DOCTYPE svg><svg/>"},
                 {"image/png", "not a png"}}) {
            put(bad.first, bad.second);
            QVERIFY(!d.pasteFromClipboard());
            QCOMPARE(d.projectBytes(), before);
            QCOMPARE(d.historyRevision(), revision);
            QCOMPARE(resets.count(), 0);
        }
        text(QString(1001, 'x'));
        QVERIFY(!d.pasteFromClipboard());
        QCOMPARE(d.projectBytes(), before);
        auto *mixed = new QMimeData;
        mixed->setData(mime, "broken");
        mixed->setText("Fallback must not hide corruption");
        QGuiApplication::clipboard()->setMimeData(mixed);
        QVERIFY(!d.pasteFromClipboard());
        QCOMPARE(d.projectBytes(), before);
    }
    void sharedLargePathsAreRejectedBeforeExpensiveSerialization() {
        Document d;
        d.newProject();
        d.addShape("rect");
        auto root = QJsonDocument::fromJson(d.projectBytes()).object();
        auto objects = root["objects"].toArray();
        auto object = objects[0].toObject();
        QPainterPath path(QPointF(0, .25));
        for (int i = 1; i < 50000; ++i)
            path.lineTo(i / 50000., i % 2 ? .75 : .25);
        object["kind"] = "path";
        object["path"] = VectorUtil::encode(path);
        object["x"] = 10;
        object["y"] = 10;
        object["width"] = 1;
        object["height"] = 1;
        objects[0] = object;
        root["objects"] = objects;
        QVERIFY(d.restoreBytes(QJsonDocument(root).toJson()));
        QVERIFY(d.command("duplicateGrid", {{"copies", 30}, {"columns", 5}, {"gap", 0.}}));
        for (int i = 1; i < d.count(); ++i)
            if (!d.isSelected(i))
                d.toggleSelection(i);
        QCOMPARE(d.selectedRows().size(), 30);
        const auto revision = d.historyRevision();
        text("previous clipboard");
        QVERIFY(!d.copySelectionToClipboard(true));
        QCOMPARE(d.count(), 30);
        QCOMPARE(d.historyRevision(), revision);
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("previous clipboard"));
    }
    void clipboardFailuresNeverDestroyCutSelectionAndCapacityIsAtomic() {
        Document source;
        source.newProject();
        source.addShape("rect");
        QVERIFY(source.copySelectionToClipboard());
        const auto payload = QGuiApplication::clipboard()->mimeData()->data(mime);
        Document full;
        full.newProject();
        full.addShape("rect");
        full.setProperty("x", 10.);
        full.setProperty("y", 10.);
        full.setProperty("width", 1.);
        full.setProperty("height", 1.);
        full.addShape("ellipse");
        full.setProperty("x", 10.);
        full.setProperty("y", 10.);
        full.setProperty("width", 1.);
        full.setProperty("height", 1.);
        full.toggleSelection(0);
        QVERIFY(full.command("duplicateGrid", {{"columns", 40}, {"copies", 1000}, {"gap", 0.}}));
        QCOMPARE(full.count(), 2000);
        const auto before = full.projectBytes();
        const auto revision = full.historyRevision();
        QSignalSpy resets(&full, &QAbstractItemModel::modelReset);
        put(mime, payload);
        QVERIFY(!full.pasteFromClipboard());
        QCOMPARE(full.projectBytes(), before);
        QCOMPARE(full.historyRevision(), revision);
        QCOMPARE(resets.count(), 0);
        source.select(-1);
        QVERIFY(!source.copySelectionToClipboard(true));
        QCOMPARE(source.count(), 1);
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->data(mime), payload);
    }
};
QTEST_MAIN(ClipboardTest)
#include "clipboard_test.moc"
