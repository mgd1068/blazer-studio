#include "artlibrary.h"
#include "job.h"
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
class ArtLibraryTest : public QObject {
    Q_OBJECT
    Artwork rectangle(QString id = "one") {
        Artwork a;
        a.id = id;
        a.name = "Private object name";
        a.kind = "rect";
        a.bounds = {10, 20, 20, 10};
        a.layer = 1;
        a.settings = {{"power", 100}, {"speed", 1}, {"passes", 100}};
        a.extraOperations = {0, 1};
        return a;
    }
    QByteArray bytes(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    GeometryAssetCodec::Encoded encodeRect() {
        return GeometryAssetCodec::encode({rectangle()}, true);
    }
  private slots:
    void independentBoundsPathsRolesGroupsAndStrippedProcessing() {
        auto a = rectangle();
        a.groupId = "source-group";
        a.groupName = "group";
        auto b = rectangle("two");
        b.kind = "path";
        b.bounds = {35, 22, 5, 5};
        b.path.moveTo(0, 0);
        b.path.lineTo(1, 0);
        b.path.lineTo(1, 1);
        b.toolLayer = 2;
        b.groupId = a.groupId;
        b.groupName = a.groupName;
        auto helper = a;
        helper.id = "three";
        helper.toolLayer = 1;
        const auto encoded = GeometryAssetCodec::encode({a, b, helper}, true);
        QVERIFY2(encoded.valid(), qPrintable(encoded.error));
        QVERIFY(!encoded.bytes.contains("power"));
        QVERIFY(!encoded.bytes.contains("passes"));
        QVERIFY(!encoded.bytes.contains("extraOperations"));
        QVERIFY(!encoded.bytes.contains("Private"));
        const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
        QVERIFY2(decoded.valid(), qPrintable(decoded.error));
        QCOMPARE(decoded.boundsMm, QRectF(0, 0, 30, 10));
        QCOMPARE(decoded.items[0].bounds, QRectF(0, 0, 20, 10));
        QCOMPARE(decoded.items[1].bounds, QRectF(25, 2, 5, 5));
        QCOMPARE(decoded.sourceRoles, QStringList({"graphic", "T2", "T1"}));
        QCOMPARE(decoded.items[0].toolLayer, 1);
        QCOMPARE(decoded.items[1].toolLayer, 2);
        QCOMPARE(decoded.items[2].toolLayer, 1);
        for (const auto &art : decoded.items) {
            QVERIFY(art.settings.isEmpty());
            QVERIFY(art.extraOperations.isEmpty());
            QVERIFY(art.text.isEmpty());
        }
        QString error;
        const auto path = JobPlanner::geometry(decoded.items[1], error, false);
        QVERIFY(error.isEmpty());
        QCOMPARE(QPointF(path.elementAt(0)), QPointF(25, 2));
        QCOMPARE(QPointF(path.elementAt(1)), QPointF(30, 2));
        QCOMPARE(QPointF(path.elementAt(2)), QPointF(30, 7));
        QVERIFY(decoded.items[0].id != a.id);
        QCOMPARE(decoded.items[0].groupId, decoded.items[1].groupId);
        const auto another = GeometryAssetCodec::decode(encoded.bytes);
        QVERIFY(another.items[0].id != decoded.items[0].id);
        QVERIFY(another.items[0].groupId != decoded.items[0].groupId);
        QCOMPARE(another.contentHash, decoded.contentHash);
    }
    void textAndImagesRequireConversionAndIndependentPngPixelsMatch() {
        auto text = rectangle();
        text.kind = "text";
        text.text = "HELLO";
        text.bounds = {10, 20, 40, 10};
        text.style = {{"font", "DejaVu Sans"}};
        const auto review = GeometryAssetCodec::review({text});
        QVERIFY(review.valid());
        QVERIFY(review.conversionRequired);
        QVERIFY(!GeometryAssetCodec::encode({text}).valid());
        const auto encodedText = GeometryAssetCodec::encode({text}, true);
        QVERIFY2(encodedText.valid(), qPrintable(encodedText.error));
        QVERIFY(!encodedText.bytes.contains("HELLO"));
        QVERIFY(!encodedText.bytes.contains("DejaVu"));
        const auto decodedText = GeometryAssetCodec::decode(encodedText.bytes);
        QCOMPARE(decodedText.items.first().kind, QString("path"));
        QVERIFY(std::abs(decodedText.boundsMm.width() - 40) < 1e-8);
        QVERIFY(std::abs(decodedText.boundsMm.height() - 10) < 1e-8);
        QImage image(2, 2, QImage::Format_ARGB32);
        image.setText("privateSourcePath", "/private/source.png");
        image.setPixelColor(0, 0, Qt::red);
        image.setPixelColor(1, 0, QColor(100, 100, 100));
        image.setPixelColor(0, 1, Qt::blue);
        image.setPixelColor(1, 1, QColor(200, 200, 200));
        auto art = rectangle();
        art.kind = "image";
        art.reliefAsset = ReliefAsset::fromImage(image);
        art.style = {{"imageMode", "threshold"},
                     {"threshold", 128},
                     {"cropX", 1},
                     {"cropY", 0},
                     {"cropW", 1},
                     {"cropH", 2}};
        QVERIFY(!GeometryAssetCodec::encode({art}).valid());
        const auto encoded = GeometryAssetCodec::encode({art}, true);
        QVERIFY2(encoded.valid(), qPrintable(encoded.error));
        const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
        QCOMPARE(decoded.items.first().bounds, QRectF(0, 0, 20, 10));
        const auto png = decoded.items.first().reliefAsset->image;
        QCOMPARE(png.size(), QSize(1, 2));
        QVERIFY(png.textKeys().isEmpty());
        QCOMPARE(png.pixelColor(0, 0), QColor(Qt::black));
        QCOMPARE(png.pixelColor(0, 1), QColor(Qt::white));
        QVERIFY(!encoded.bytes.contains("threshold"));
        QVERIFY(!encoded.bytes.contains("cropX"));
        QString error;
        const auto thumbnail = GeometryAssetCodec::thumbnail(encoded.bytes, error);
        QVERIFY2(!thumbnail.isEmpty(), qPrintable(error));
        const auto preview = QImage::fromData(thumbnail, "PNG");
        QCOMPARE(preview.size(), QSize(256, 256));
        QCOMPARE(preview.pixelColor(128, 90), QColor(Qt::black));
        QCOMPARE(preview.pixelColor(128, 165), QColor(Qt::white));
    }
    void svgActualTransformAndIndependentOpenLinesArePreserved() {
        auto svg = rectangle();
        svg.kind = "svg";
        svg.bounds = {50, 60, 20, 20};
        QString error;
        svg.asset =
            SvgAsset::parse("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='20mm' "
                            "viewBox='0 0 20 20'><g transform='translate(2 3)'><path d='M0 0L10 "
                            "0L10 10' fill='none' stroke='black'/></g></svg>",
                            error);
        QVERIFY(svg.asset);
        const auto encoded = GeometryAssetCodec::encode({svg}, true);
        QVERIFY2(encoded.valid(), qPrintable(encoded.error));
        const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
        QCOMPARE(decoded.boundsMm, QRectF(0, 0, 10, 10));
        const auto path = JobPlanner::geometry(decoded.items.first(), error, false);
        QVERIFY(error.isEmpty());
        QCOMPARE(QPointF(path.elementAt(0)), QPointF(0, 10));
        QCOMPARE(QPointF(path.elementAt(1)), QPointF(10, 10));
        QCOMPARE(QPointF(path.elementAt(2)), QPointF(10, 0));
        auto line = rectangle();
        line.kind = "path";
        line.path.moveTo(0, 0);
        line.path.lineTo(1, 0);
        auto second = line;
        second.id = "two";
        second.bounds.translate(0, 10);
        const auto lines =
            GeometryAssetCodec::decode(GeometryAssetCodec::encode({line, second}, true).bytes);
        QVERIFY2(lines.valid(), qPrintable(lines.error));
        QVERIFY(std::abs(lines.boundsMm.height() - 10.1) < 1e-9);
        QCOMPARE(lines.boundsMm.topLeft(), QPointF(0, 0));
        for (const auto &line : lines.items)
            QVERIFY(line.bounds.top() >= -1e-9);
        const auto linePath = JobPlanner::geometry(lines.items[1], error, false);
        QVERIFY(QLineF(QPointF(linePath.elementAt(0)), QPointF(0, 10.05)).length() < 1e-9);
    }
    void cubicControlsOutsideCurveBoundsRemainValidGeometry() {
        auto curve = rectangle();
        curve.kind = "path";
        curve.path.moveTo(0, 0);
        curve.path.cubicTo(0, 2, 1, 2, 1, 0);
        const auto encoded = GeometryAssetCodec::encode({curve}, true);
        QVERIFY2(encoded.valid(), qPrintable(encoded.error));
        const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
        QVERIFY2(decoded.valid(), qPrintable(decoded.error));
        QCOMPARE(decoded.boundsMm, QRectF(0, 0, 20, 15));
        QString error;
        const auto actual = JobPlanner::geometry(decoded.items.first(), error, false);
        QVERIFY(error.isEmpty());
        QCOMPARE(QPointF(actual.elementAt(1)), QPointF(0, 20));
        QCOMPARE(QPointF(actual.elementAt(2)), QPointF(20, 20));
        QVERIFY(QLineF(actual.pointAtPercent(.5), QPointF(10, 15)).length() < 1e-9);
    }
    void mixedOpenClosedContoursPreserveFillRuleAndIndependentInteriorPoints() {
        auto ring = rectangle();
        ring.kind = "path";
        ring.path.addRect({0, 0, 1, 1});
        ring.path.addRect({.2, .2, .6, .6});
        auto line = rectangle("line");
        line.kind = "path";
        line.bounds = {40, 22, 10, 10};
        line.path.moveTo(0, 0);
        line.path.lineTo(1, 0);
        QByteArray priorHash;
        for (const auto rule : {Qt::OddEvenFill, Qt::WindingFill}) {
            ring.path.setFillRule(rule);
            const auto review = GeometryAssetCodec::review({ring, line});
            QVERIFY(review.valid());
            QVERIFY(review.conversionRequired);
            QVERIFY(!GeometryAssetCodec::encode({ring, line}).valid());
            const auto encoded = GeometryAssetCodec::encode({ring, line}, true);
            QVERIFY2(encoded.valid(), qPrintable(encoded.error));
            const auto decoded = GeometryAssetCodec::decode(encoded.bytes);
            QVERIFY(decoded.valid());
            QCOMPARE(decoded.boundsMm, QRectF(0, 0, 40, 10));
            QString error;
            const auto region = JobPlanner::geometry(decoded.items[0], error, false);
            QVERIFY(error.isEmpty());
            QCOMPARE(region.fillRule(), rule);
            QVERIFY(region.contains(QPointF(2, 1)));
            QCOMPARE(region.contains(QPointF(10, 5)), rule == Qt::WindingFill);
            const auto open = JobPlanner::geometry(decoded.items[1], error, false);
            QCOMPARE(open.elementCount(), 2);
            QVERIFY(QLineF(QPointF(open.elementAt(0)), QPointF(30, 2)).length() < 1e-9);
            QVERIFY(QLineF(QPointF(open.elementAt(1)), QPointF(40, 2)).length() < 1e-9);
            if (!priorHash.isEmpty())
                QVERIFY(encoded.contentHash != priorHash);
            priorHash = encoded.contentHash;
        }
    }
    void malformedUnsupportedAndBudgetInputsFailAtomically() {
        auto good = rectangle();
        for (int variant = 0; variant < 5; ++variant) {
            auto bad = rectangle("other");
            if (variant == 0)
                bad.visible = false;
            if (variant == 1)
                bad.page = 1;
            if (variant == 2)
                bad.kind = "relief";
            if (variant == 3)
                bad.id = good.id;
            if (variant == 4)
                bad.kind = "unknown";
            QVERIFY(!GeometryAssetCodec::review({good, bad}).valid());
            const auto result = GeometryAssetCodec::encode({good, bad}, true);
            QVERIFY(!result.valid());
            QVERIFY(result.bytes.isEmpty());
        }
        const auto valid = encodeRect().bytes;
        QVERIFY(!valid.isEmpty());
        for (int variant = 0; variant < 6; ++variant) {
            auto root = QJsonDocument::fromJson(valid).object();
            auto rows = root["items"].toArray();
            auto row = rows[0].toObject();
            if (variant == 0)
                root["settings"] = QJsonObject{{"power", 100}};
            if (variant == 1)
                row["role"] = "output";
            if (variant == 2)
                row["index"] = 1;
            if (variant == 3)
                row["group"] = 1;
            if (variant == 4)
                row["settings"] = QJsonObject{};
            if (variant == 5)
                root["widthMm"] = 0;
            rows[0] = row;
            root["items"] = rows;
            const auto decoded = GeometryAssetCodec::decode(QJsonDocument(root).toJson());
            QVERIFY(!decoded.valid());
            QVERIFY(decoded.items.isEmpty());
        }
        auto huge = good;
        huge.kind = "path";
        huge.path.moveTo(0, 0);
        for (int i = 0; i < 100001; ++i)
            huge.path.lineTo(i % 2, (i % 3) / 3.);
        QVERIFY(!GeometryAssetCodec::encode({huge}).valid());
        QVERIFY(!GeometryAssetCodec::decode(QByteArray(16 * 1024 * 1024 + 1, 'x')).valid());
        const auto token = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!GeometryAssetCodec::encode({good}, false, token).valid());
        QVERIFY(!GeometryAssetCodec::decode(valid, token).valid());
        auto unsafe = good;
        unsafe.kind = "svg";
        auto asset = std::make_shared<SvgAsset>();
        asset->bytes =
            "<svg xmlns='http://www.w3.org/2000/svg'><image href='file:///never/read.png'/></svg>";
        unsafe.asset = asset;
        QVERIFY(!GeometryAssetCodec::review({unsafe}).valid());
        QString error;
        auto sharedSvg =
            SvgAsset::parse(QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' "
                                       "height='10mm'><rect width='10' height='10'/><!--") +
                                QByteArray(950000, 'a') + "--></svg>",
                            error);
        QVERIFY2(sharedSvg, qPrintable(error));
        QList<Artwork> repeated;
        for (int i = 0; i < 9; ++i) {
            auto entry = rectangle(QString::number(i));
            entry.kind = "svg";
            entry.asset = sharedSvg;
            repeated.append(entry);
        }
        QVERIFY(!GeometryAssetCodec::review(repeated).valid());
    }
    void persistentSmallMetadataUnicodeSearchAndSourceImmutableAfterRenameDelete() {
        QTemporaryDir directory;
        const auto path = directory.filePath("library.blazerart");
        const auto encoded = encodeRect();
        QVERIFY(encoded.valid());
        QString id;
        QByteArray original;
        {
            ArtLibrary library(path);
            QVERIFY(library.writable());
            QVERIFY(!QFile::exists(path));
            id = library.add("  Größe <test>  ", {"Äpfel", "TEST", "test"}, encoded.bytes);
            QVERIFY2(!id.isEmpty(), qPrintable(library.message()));
            original = library.geometry(id);
            QCOMPARE(library.search("äPFEL").size(), 1);
            QCOMPARE(library.search("größe").size(), 1);
            const auto metadata = library.entries().first().toMap();
            QCOMPARE(metadata["tags"].toStringList(), QStringList({"Äpfel", "TEST"}));
            QVERIFY(!metadata.contains("geometry"));
            QVERIFY(!metadata.contains("png"));
            QVERIFY(!library.thumbnail(id).isEmpty());
            QVERIFY(library.updateMetadata(id, "Renamed", {"new"}));
            QCOMPARE(library.geometry(id), original);
            QCOMPARE(library.entries().first().toMap()["contentHash"].toString(),
                     QString::fromLatin1(encoded.contentHash));
            const auto permissions = QFile::permissions(path);
            QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                     QFileDevice::ReadOther | QFileDevice::WriteOther)));
        }
        {
            ArtLibrary loaded(path);
            QVERIFY(loaded.writable());
            QCOMPARE(loaded.entries().size(), 1);
            QCOMPARE(loaded.geometry(id), original);
            QCOMPARE(loaded.search("new").size(), 1);
            QVERIFY(loaded.remove(id));
            QVERIFY(loaded.entries().isEmpty());
        }
        QVERIFY(GeometryAssetCodec::decode(original).valid());
        ArtLibrary empty(path);
        QVERIFY(empty.writable());
        QVERIFY(empty.entries().isEmpty());
    }
    void concurrentExternalMutationCorruptionAndFailedWritesPreserveFiles() {
        QTemporaryDir directory;
        const auto path = directory.filePath("library.blazerart");
        const auto encoded = encodeRect();
        QByteArray valid;
        {
            ArtLibrary first(path);
            const auto id = first.add("One", {}, encoded.bytes);
            QVERIFY(!id.isEmpty());
            valid = bytes(path);
            ArtLibrary competing(path);
            QVERIFY(!competing.writable());
            QVERIFY(competing.add("Two", {}, encoded.bytes).isEmpty());
            QCOMPARE(bytes(path), valid);
            QFile changed(path);
            QVERIFY(changed.open(QIODevice::WriteOnly));
            changed.write("external mutation");
            changed.close();
            QVERIFY(!first.updateMetadata(id, "Rename", {}));
            QVERIFY(!first.writable());
            QVERIFY(first.geometry(id).isEmpty());
            QCOMPARE(bytes(path), QByteArray("external mutation"));
        }
        {
            ArtLibrary corrupt(path);
            QVERIFY(!corrupt.writable());
            QVERIFY(corrupt.entries().isEmpty());
            QVERIFY(corrupt.add("No", {}, encoded.bytes).isEmpty());
            QCOMPARE(bytes(path), QByteArray("external mutation"));
        }
        auto root = QJsonDocument::fromJson(valid).object();
        auto rows = root["entries"].toArray();
        auto entry = rows[0].toObject();
        entry["contentHash"] = QString(64, 'a');
        rows[0] = entry;
        root["entries"] = rows;
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(root).toJson());
        }
        const auto tampered = bytes(path);
        {
            ArtLibrary loaded(path);
            QVERIFY(!loaded.writable());
            QVERIFY(loaded.entries().isEmpty());
            QCOMPARE(bytes(path), tampered);
        }
        const auto missing = directory.filePath("missing/subdir/library.blazerart");
        ArtLibrary unwritable(missing);
        const auto parent = directory.filePath("missing/subdir");
        QVERIFY(QDir().rename(parent, parent + "-held"));
        {
            QFile blockedParent(parent);
            QVERIFY(blockedParent.open(QIODevice::WriteOnly));
            blockedParent.write("regular file blocks parent traversal");
        }
        QVERIFY(unwritable.add("Failure", {}, encoded.bytes).isEmpty());
        QVERIFY(unwritable.entries().isEmpty());
        QVERIFY(QFile::remove(parent));
        QVERIFY(QDir().rename(parent + "-held", parent)); // Restore lock path before destructor.
        QVERIFY(!unwritable.add("Explicit retry", {}, encoded.bytes).isEmpty());
    }
    void capacityDuplicateIdsAndOversizedDiskFilesAreProtected() {
        QTemporaryDir directory;
        const auto path = directory.filePath("library.blazerart");
        {
            ArtLibrary initial(path);
            QVERIFY(!initial.add("One", {}, encodeRect().bytes).isEmpty());
        }
        const auto initial = QJsonDocument::fromJson(bytes(path)).object();
        for (int variant = 0; variant < 3; ++variant) {
            auto root = initial;
            auto rows = root["entries"].toArray();
            if (variant == 0)
                rows.append(rows.first());
            if (variant == 1) {
                const auto prototype = rows.first().toObject();
                rows = QJsonArray{};
                for (int i = 0; i < 257; ++i) {
                    auto row = prototype;
                    row["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    rows.append(row);
                }
            }
            root["entries"] = rows;
            if (variant == 2)
                root["deviceProfile"] = QJsonObject{};
            {
                QFile file(path);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(QJsonDocument(root).toJson());
            }
            const auto rejectedBytes = bytes(path);
            ArtLibrary rejected(path);
            QVERIFY(!rejected.writable());
            QVERIFY(rejected.entries().isEmpty());
            QVERIFY(rejected.add("Must fail", {}, encodeRect().bytes).isEmpty());
            QCOMPARE(bytes(path), rejectedBytes);
        }
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.resize(64 * 1024 * 1024 + 1));
        }
        ArtLibrary oversized(path);
        QVERIFY(!oversized.writable());
        QVERIFY(oversized.entries().isEmpty());
        QCOMPARE(QFileInfo(path).size(), qint64(64 * 1024 * 1024 + 1));
    }
};
QTEST_MAIN(ArtLibraryTest)
#include "artlibrary_test.moc"
