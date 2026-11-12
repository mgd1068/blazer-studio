#include "document.h"
#include "geometry.h"
#include "materiallibrary.h"
#include "simulator.h"
#include "workspace.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class CoreTest : public QObject {
    Q_OBJECT
  private slots:
    void projectMetadataHistoryAndRoundtrip() {
        QTemporaryDir dir;
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto url = QUrl::fromLocalFile(dir.filePath("notes.blazer"));
        QVERIFY(d.save(url));
        const auto original = d.projectBytes();
        const auto gcode = d.job().gcode();
        const QString notes = "Kraftpapier 80 × 80 mm\nDicke unbekannt.\n<b>Kein HTML</b> – heiß.";
        QVERIFY(d.setMetadata("  Versuch Ä  ", notes));
        QCOMPARE(d.title(), QString("Versuch Ä"));
        QCOMPARE(d.notes(), notes);
        QVERIFY(d.dirty());
        QCOMPARE(d.job().gcode(), gcode);
        const auto edited = d.projectBytes();
        QVERIFY(d.setMetadata(d.title(), d.notes())); // No extra history entry.
        d.undo();
        QCOMPARE(d.projectBytes(), original);
        QVERIFY(!d.dirty());
        d.redo();
        QCOMPARE(d.projectBytes(), edited);
        QVERIFY(d.save(url));
        QVERIFY(!d.dirty());
        Document loaded;
        QVERIFY(loaded.open(url));
        QCOMPARE(loaded.title(), d.title());
        QCOMPARE(loaded.notes(), notes);
        QCOMPARE(loaded.projectBytes(), edited);
        QCOMPARE(loaded.job().gcode(), gcode);
        QVERIFY(!loaded.dirty());
        Document recovered;
        QVERIFY(recovered.restoreBytes(edited));
        QCOMPARE(recovered.notes(), notes);
        QVERIFY(recovered.dirty());
        Document destination;
        destination.newProject();
        QVERIFY(destination.setMetadata("Zielprojekt", "Eigene Angaben behalten"));
        QVERIFY(destination.importProject(url));
        QCOMPARE(destination.count(), 1);
        QCOMPARE(destination.title(), QString("Zielprojekt"));
        QCOMPARE(destination.notes(), QString("Eigene Angaben behalten"));
        destination.undo();
        QCOMPARE(destination.count(), 0);
        QCOMPARE(destination.notes(), QString("Eigene Angaben behalten"));
        destination.redo();
        QCOMPARE(destination.count(), 1);
        QCOMPARE(destination.notes(), QString("Eigene Angaben behalten"));
        loaded.newProject();
        QVERIFY(loaded.notes().isEmpty());
        QVERIFY(loaded.setMetadata("Neu", "Keine alten Notizen"));
        loaded.demo();
        QVERIFY(loaded.notes().isEmpty());
    }
    void projectMetadataValidationIsAtomic() {
        Document d;
        const auto original = d.projectBytes();
        for (const auto &title : {QString(), QString("   "), QString(1001, 'x'),
                                  QString("Mehr\nZeilen"), QString("Mehr\rZeilen")}) {
            QVERIFY(!d.setMetadata(title, "Notiz"));
            QCOMPARE(d.projectBytes(), original);
            QVERIFY(!d.dirty());
        }
        QVERIFY(!d.setMetadata("Gültig", QString(16001, 'x')));
        QCOMPARE(d.projectBytes(), original);
        QVERIFY(d.setMetadata(QString(1000, 'x'), QString(16000, 'n')));
        const auto valid = d.projectBytes();
        const auto root = QJsonDocument::fromJson(valid).object();
        QCOMPARE(root["version"].toInt(), 8);
        for (int variant = 0; variant < 4; ++variant) {
            auto bad = root;
            if (variant == 0)
                bad.remove("notes");
            if (variant == 1)
                bad["notes"] = 42;
            if (variant == 2)
                bad["notes"] = QString(16001, 'x');
            if (variant == 3)
                bad["version"] = 9;
            QVERIFY(!d.restoreBytes(QJsonDocument(bad).toJson()));
            QCOMPARE(d.projectBytes(), valid);
        }
    }
    void projectMetadataReadsLegacyWithoutNotes() {
        Document d;
        auto legacy = QJsonDocument::fromJson(d.projectBytes()).object();
        legacy["version"] = 7;
        legacy.remove("notes");
        // Legacy names must roundtrip without silently normalizing old content.
        for (const auto &title : {QString(), QString("Name\nmit Zeile")}) {
            legacy["title"] = title;
            QVERIFY(d.restoreBytes(QJsonDocument(legacy).toJson()));
            QCOMPARE(d.title(), title);
            QVERIFY(d.notes().isEmpty());
            Document roundtrip;
            QVERIFY(roundtrip.restoreBytes(d.projectBytes()));
            QCOMPARE(roundtrip.title(), title);
        }
    }
    void beginnerProfileContract() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setObjectSetting("power", 91);
        auto before = d.projectBytes();
        auto profile = MaterialLibrary::example();
        auto cut = profile["cut"].toMap();
        cut["enabled"] = false;
        profile["cut"] = cut;
        QVERIFY(d.applyBasicMaterial(profile));
        QCOMPARE(d.items()[0].layer, 0);
        QVERIFY(d.items()[0].settings.isEmpty());
        for (int i = 0; i < 2; ++i)
            for (const QString key : {"speed", "power", "passes", "interval", "enabled"})
                QCOMPARE(d.layers()[i].toMap()[key], profile[i ? "cut" : "engrave"].toMap()[key]);
        QVERIFY(!d.beginnerSummary()["modified"].toBool());
        d.undo();
        QCOMPARE(d.projectBytes(), before);
        d.redo();
        d.setLayerSetting(0, "overscan", 2.);
        QVERIFY(d.beginnerSummary()["requiresAdvanced"].toBool());
        before = d.projectBytes();
        QVERIFY(!d.applyBasicMaterial(profile));
        QCOMPARE(d.projectBytes(), before);
        d.undo();
        auto bad = profile;
        auto engraving = bad["engrave"].toMap();
        engraving["power"] = 101.;
        bad["engrave"] = engraving;
        before = d.projectBytes();
        QVERIFY(!d.applyBasicMaterial(bad));
        QCOMPARE(d.projectBytes(), before);
        d.assignOperations({0, d.addOperation("Extra", "Gravur")});
        QVERIFY(d.beginnerSummary()["requiresAdvanced"].toBool());
        before = d.projectBytes();
        QVERIFY(!d.applyBasicMaterial(profile));
        QCOMPARE(d.projectBytes(), before);
    }
    void beginnerUndoAtHistoryLimit() {
        Document d;
        d.newProject();
        d.addShape("rect");
        for (int i = 0; i < 65; ++i)
            d.setProperty("x", 10. + i);
        d.setObjectSetting("power", 91.);
        const auto before = d.projectBytes();
        auto profile = MaterialLibrary::example();
        profile["name"] = "Neue Profilwerte";
        QVERIFY(d.applyBasicMaterial(profile));
        QVERIFY(d.items()[0].settings.isEmpty());
        d.undo();
        QCOMPARE(d.projectBytes(), before);
    }
    void beginnerPhotoNeedsEngraving() {
        QTemporaryDir temp;
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::black);
        auto path = temp.filePath("photo.png");
        QVERIFY(image.save(path));
        Document d;
        d.newProject();
        QVERIFY(d.importImage(QUrl::fromLocalFile(path)));
        auto profile = MaterialLibrary::example();
        auto engraving = profile["engrave"].toMap();
        engraving["enabled"] = false;
        profile["engrave"] = engraving;
        auto before = d.projectBytes();
        QVERIFY(!d.applyBasicMaterial(profile));
        QCOMPARE(d.projectBytes(), before);
    }
    void materialLibraryPersistence() {
        QTemporaryDir dir;
        const auto path = dir.filePath("materials.json");
        MaterialLibrary library(path);
        QCOMPARE(library.count(), 65);
        auto profile = library.profileAt(1);
        QCOMPARE(profile["cut"].toMap()["speed"].toDouble(), 250.);
        profile["name"] = "Mein Sperrholz";
        auto cut = profile["cut"].toMap();
        cut["power"] = 42.5;
        profile["cut"] = cut;
        const auto id = library.saveProfile(profile);
        QVERIFY(!id.isEmpty());
        MaterialLibrary reopened(path);
        QCOMPARE(reopened.profileAt(reopened.indexOf(id))["name"].toString(), "Mein Sperrholz");
        QCOMPARE(reopened.profileAt(reopened.indexOf(id))["cut"].toMap()["power"].toDouble(), 42.5);
        const auto copy = reopened.duplicateProfile(reopened.indexOf(id));
        QVERIFY(!copy.isEmpty());
        QVERIFY(copy != id);
        QCOMPARE(reopened.count(), 66);
        QVERIFY(reopened.removeProfile(copy));
        QCOMPARE(reopened.count(), 65);
        profile["name"] = " ";
        QVERIFY(reopened.saveProfile(profile).isEmpty());
        QCOMPARE(reopened.count(), 65);
        for (const auto &name : reopened.names())
            QVERIFY(!name.trimmed().isEmpty());
    }
    void toolHeadFiltering() {
        QTemporaryDir dir;
        MaterialLibrary library(dir.filePath("materials.json"));
        QCOMPARE(library.modulePowers(), QVariantList({1., 5., 10.}));
        QCOMPARE(library.rowsForModule(5).size(), 27);
        QCOMPARE(library.rowsForModule(10).size(), 25);
        QCOMPARE(library.rowsForModule(1).size(), 13);
        for (double watts : {1., 5., 10.}) {
            for (const auto &row : library.rowsForModule(watts))
                QCOMPARE(library.profileAt(row.toInt())["moduleW"].toDouble(), watts);
        }
        QVERIFY(library.rowsForModule(20).isEmpty());
        QVERIFY(library.rowsForModule(std::numeric_limits<double>::quiet_NaN()).isEmpty());
        auto profile = library.newProfile();
        profile["moduleW"] = 20.;
        const auto id = library.saveProfile(profile);
        QVERIFY(!id.isEmpty());
        QVERIFY(library.modulePowers().contains(20.));
        QCOMPARE(library.rowsForModule(20), QVariantList({library.indexOf(id)}));
        profile = library.profileAt(library.indexOf(id));
        profile["moduleW"] = 10.;
        QVERIFY(!library.saveProfile(profile).isEmpty());
        QVERIFY(!library.modulePowers().contains(20.));
        QCOMPARE(library.rowsForModule(10).size(), 26);
        QVERIFY(library.removeProfile(id));
        QCOMPARE(library.rowsForModule(10).size(), 25);
    }
    void completeManufacturerLibrary() {
        QTemporaryDir dir;
        MaterialLibrary library(dir.filePath("materials.json"));
        QFile file(":/catalog/profiles/manufacturer-materials.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto catalog = QJsonDocument::fromJson(file.readAll()).object();
        int sourceCount = 0;
        for (const auto &tableValue : catalog["tables"].toArray()) {
            auto table = tableValue.toObject();
            for (const auto &entryValue : table["entries"].toArray()) {
                auto source = entryValue.toObject();
                int matches = 0;
                for (int i = 0; i < library.count(); ++i) {
                    auto profile = library.profileAt(i);
                    if (profile["sourceTable"] != table["sourceTitle"].toVariant() ||
                        profile["sourceParameters"].toMap() != source.toVariantMap())
                        continue;
                    ++matches;
                    QString error;
                    QVERIFY2(MaterialLibrary::validate(profile, error), qPrintable(error));
                    const auto mode = table["operation"].toString();
                    auto values = profile[mode].toMap();
                    QCOMPARE(values["speed"].toDouble(), source["speedMmMin"].toDouble());
                    QCOMPARE(values["power"].toDouble(), source["powerPercent"].toDouble());
                    QVERIFY(values["enabled"].toBool());
                    QVERIFY(!profile[mode == "engrave" ? "cut" : "engrave"]
                                 .toMap()["enabled"]
                                 .toBool());
                    QCOMPARE(profile["moduleW"].toInt(), table["modulePowerW"].toInt());
                    if (mode == "engrave") {
                        QCOMPARE(values["interval"].toDouble(), source["intervalMm"].toDouble());
                        QVERIFY(profile["sourceParameters"].toMap()["passes"].isNull());
                        QVERIFY(profile["notes"].toString().contains("Arbeitswert"));
                    } else {
                        QCOMPARE(values["passes"].toInt(), source["passes"].toInt());
                    }
                    Document d;
                    QVERIFY(d.applyMaterial(profile));
                }
                QCOMPARE(matches, 1);
                ++sourceCount;
            }
        }
        QCOMPARE(sourceCount, 64);
        QCOMPARE(library.profileAt(library.indexOf("vendor-copy-pro5w-plywood-3"))["thicknessMm"]
                     .toDouble(),
                 3.);
        QCOMPARE(
            library.profileAt(library.indexOf("vendor-m3pro-5w-cut-9"))["thicknessMm"].toDouble(),
            3.5);
    }
    void existingLibraryCatalogMigration() {
        QTemporaryDir dir;
        auto path = dir.filePath("materials.json");
        MaterialLibrary defaults(dir.filePath("unused.json"));
        auto edited = defaults.profileAt(1);
        edited["name"] = "Mein eigener Schnitt";
        auto cut = edited["cut"].toMap();
        cut["power"] = 33.;
        edited["cut"] = cut;
        auto own = MaterialLibrary::example();
        own["id"] = "my-custom-profile";
        QJsonObject legacy{{"schemaVersion", 1},
                           {"profiles", QJsonArray::fromVariantList({own, edited})}};
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const auto original = QJsonDocument(legacy).toJson();
        f.write(original);
        f.close();
        MaterialLibrary migrated(path);
        QCOMPARE(migrated.count(), 65);
        QCOMPARE(migrated.profileAt(migrated.indexOf(edited["id"].toString()))["cut"]
                     .toMap()["power"]
                     .toDouble(),
                 33.);
        QCOMPARE(migrated.profileAt(0)["id"].toString(), "my-custom-profile");
        QFile backup(path + ".before-manufacturer-v1.bak");
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);
        const auto id = "vendor-m3pro-1w-engrave-13";
        QVERIFY(migrated.removeProfile(id));
        MaterialLibrary reopened(path);
        QCOMPARE(reopened.count(), 64);
        QCOMPARE(reopened.indexOf(id), -1);
        auto copy = reopened.duplicateProfile(0);
        QVERIFY(!copy.isEmpty());
        MaterialLibrary reopenedAgain(path);
        QCOMPARE(reopenedAgain.count(), 65);
    }
    void damagedLibraryPreserved() {
        QTemporaryDir dir;
        auto path = dir.filePath("materials.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("broken");
        f.close();
        MaterialLibrary library(path);
        QVERIFY(library.saveProfile(MaterialLibrary::example()).isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("broken"));
        MaterialLibrary unwritable(dir.path());
        QVERIFY(unwritable.saveProfile(MaterialLibrary::example()).isEmpty());
    }
    void appliedMaterialSnapshot() {
        QTemporaryDir dir;
        MaterialLibrary library(dir.filePath("materials.json"));
        Document d;
        QVERIFY(d.applyMaterial(library.profileAt(1)));
        QCOMPARE(d.layers()[1].toMap()["speed"].toDouble(), 250.);
        QVERIFY(!d.layers()[0].toMap()["enabled"].toBool());
        QVERIFY(!d.materialModified());
        d.setLayerSetting(1, "power", 55.);
        QVERIFY(d.materialModified());
        d.undo();
        QVERIFY(!d.materialModified());
        auto url = QUrl::fromLocalFile(dir.filePath("material.blazer"));
        QVERIFY(d.save(url));
        auto p = library.profileAt(1);
        p["name"] = "Changed later";
        QVERIFY(!library.saveProfile(p).isEmpty());
        Document reopened;
        QVERIFY(reopened.open(url));
        QCOMPARE(reopened.material()["name"], d.material()["name"]);
        QVERIFY(reopened.material()["name"] != p["name"]);
        QVERIFY(!reopened.materialModified());
    }
    void modelAndCleanHistory() {
        Document d;
        QAbstractItemModelTester tester(&d, QAbstractItemModelTester::FailureReportingMode::QtTest);
        d.setProperty("x", d.selection()["x"]);
        QVERIFY(!d.canUndo());
        QVERIFY(!d.dirty());
        d.addShape("rect");
        d.duplicateSelected();
        d.removeSelected();
        d.undo();
        d.redo();
        d.undo();
        d.undo();
        d.undo();
        QVERIFY(!d.dirty());
        d.setProperty("x", 31.);
        QVERIFY(d.dirty());
        d.undo();
        QVERIFY(!d.dirty());
        d.setLayerSetting(0, "interval", 0.);
        QCOMPARE(d.layers()[0].toMap()["interval"].toDouble(), .1);
        d.newProject();
        QCOMPARE(d.count(), 0);
        QVERIFY(!d.activeBounds()["valid"].toBool());
    }
    void geometryAndFrame() {
        QCOMPARE(Geometry::worldToCanvas(QPointF(20, 30)), QPointF(20, 120));
        const QRectF bounds(10, 20, 40, 20);
        QCOMPARE(Geometry::framePoint(bounds, 0), QPointF(10, 20));
        QCOMPARE(Geometry::framePoint(bounds, 1. / 3), QPointF(50, 20));
        QCOMPARE(Geometry::framePoint(bounds, .5), QPointF(50, 40));
        QCOMPARE(Geometry::framePoint(bounds, 1), QPointF(10, 20));
        Simulator s;
        s.frame({{"valid", true}, {"x", -1.}, {"y", 0.}, {"width", 40.}, {"height", 20.}});
        QVERIFY(!s.busy());
        s.frame({{"valid", true}, {"x", 10.}, {"y", 20.}, {"width", 40.}, {"height", 20.}});
        QVERIFY(s.busy());
        QVERIFY(!s.firing());
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(), 4500);
        QCOMPARE(s.x(), 10.);
        QCOMPARE(s.y(), 20.);
        s.disconnectFault();
        s.stop();
        QVERIFY(!s.connected());
    }
    void svgUnitsAndSharedHistory() {
        QString error;
        auto svg = SvgAsset::parse(
            "<svg xmlns='http://www.w3.org/2000/svg' width='1in' height='2.54cm'/>", error);
        QVERIFY2(svg, qPrintable(error));
        QCOMPARE(svg->sizeMm, QSizeF(25.4, 25.4));
        for (const QByteArray content :
             {QByteArray("<svg><script>oops</script></svg>"),
              QByteArray("<!DOCTYPE svg [<!ENTITY x 'x'>]><svg/>"),
              QByteArray("<svg><image href='https://example.com/a'/></svg>"),
              QByteArray("<svg width='20%' height='10mm'/>")})
            QVERIFY(!SvgAsset::parse(content, error));
        QTemporaryDir dir;
        QFile f(dir.filePath("test.svg"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(svg->bytes);
        f.close();
        Document d;
        QVERIFY(d.importSvg(QUrl::fromLocalFile(f.fileName())));
        auto asset = d.items().last().asset;
        d.setProperty("x", 11.);
        d.undo();
        QCOMPARE(d.items().last().asset, asset);
        d.duplicateSelected();
        QCOMPARE(d.items().last().asset, asset);
    }
    void groupsAndTransforms() {
        Document d;
        QAbstractItemModelTester tester(&d, QAbstractItemModelTester::FailureReportingMode::QtTest);
        d.select(3);
        d.toggleSelection(4);
        QCOMPARE(d.selectedRows().size(), 2);
        const auto original = d.items();
        d.groupSelection();
        QVERIFY(d.selection()["grouped"].toBool());
        d.setProperty("name", "Beschriftung");
        const auto bounds = d.selectionBounds();
        d.setProperty("x", bounds.x() + 10.);
        QCOMPARE(d.items()[3].bounds.x(), original[3].bounds.x() + 10.);
        QCOMPARE(d.items()[4].bounds.x(), original[4].bounds.x() + 10.);
        QCOMPARE(d.items()[0].bounds, original[0].bounds);
        d.scaleSelection(200);
        QCOMPARE(d.selectionBounds().width(), bounds.width() * 2);
        QCOMPARE(d.items()[4].bounds.width(), original[4].bounds.width() * 2);
        d.undo();
        QCOMPARE(d.selectionBounds().width(), bounds.width());
        d.redo();
        const auto group = d.items()[3].groupId;
        d.duplicateSelected();
        QCOMPARE(d.count(), 7);
        QCOMPARE(d.selectedRows().size(), 2);
        QVERIFY(d.selection()["grouped"].toBool());
        QVERIFY(d.items()[5].groupId != group);
        QCOMPARE(d.items()[5].groupId, d.items()[6].groupId);
        d.removeSelected();
        QCOMPARE(d.count(), 5);
        d.undo();
        QCOMPARE(d.count(), 7);
        d.ungroupSelection();
        QVERIFY(d.items()[5].groupId.isEmpty());
        QVERIFY(d.items()[6].groupId.isEmpty());
        d.selectGroup(3);
        QCOMPARE(d.selectedRows().size(), 2);
        d.setLayer(1);
        QCOMPARE(d.items()[3].layer, 1);
        QCOMPARE(d.items()[4].layer, 1);
        d.select(4);
        QCOMPARE(d.selectedRows().size(), 1);
        d.setLayer(0);
        QCOMPARE(d.items()[3].layer, 1);
        QCOMPARE(d.items()[4].layer, 0);
        const auto before = d.items()[4].bounds;
        d.scaleSelection(0);
        d.scaleSelection(1e300);
        QCOMPARE(d.items()[4].bounds, before);
    }
    void groupPersistenceAndValidation() {
        QTemporaryDir dir;
        auto url = QUrl::fromLocalFile(dir.filePath("group.blazer"));
        Document d;
        d.select(1);
        d.toggleSelection(3);
        d.groupSelection();
        d.setProperty("name", "Meine Gruppe");
        QVERIFY(d.save(url));
        Document loaded;
        QVERIFY(loaded.open(url));
        QCOMPARE(loaded.selectedRows().size(), 2);
        QVERIFY(loaded.selection()["grouped"].toBool());
        QCOMPARE(loaded.selection()["name"].toString(), "Meine Gruppe");
        loaded.beginMove();
        auto pos = loaded.selectionBounds().topLeft();
        loaded.moveSelection(pos + QPointF(10, 20));
        loaded.finishMove();
        loaded.undo();
        QCOMPARE(loaded.selectionBounds().topLeft(), pos);
        QVERIFY(!loaded.dirty());
        QFile f(url.toLocalFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        auto arr = root["objects"].toArray();
        auto obj = arr[2].toObject();
        obj["groupName"] = "Wrong name";
        arr[2] = obj;
        root["objects"] = arr;
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();
        QVERIFY(!loaded.open(url));
        QCOMPARE(loaded.selection()["name"].toString(), "Meine Gruppe");
    }
    void regroupingPreservesValidGroups() {
        Document d;
        d.select(0);
        d.toggleSelection(1);
        d.toggleSelection(2);
        d.toggleSelection(3);
        d.groupSelection();
        d.select(1);
        d.toggleSelection(4);
        d.groupSelection();
        QTemporaryDir dir;
        auto url = QUrl::fromLocalFile(dir.filePath("regroup.blazer"));
        QVERIFY(d.save(url));
        Document loaded;
        QVERIFY(loaded.open(url));
        QCOMPARE(loaded.count(), 5);
        QCOMPARE(loaded.selectedRows().size(), 2);
        loaded.selectGroup(2);
        QCOMPARE(loaded.selectedRows().size(), 3);
    }
    void selectionRevealAndNumericalSize() {
        Document d;
        Workspace view;
        view.setWidth(700);
        view.setHeight(600);
        view.setDocument(&d);
        d.select(3);
        d.setProperty("x", 400.);
        d.setProperty("y", 300.);
        view.ensureSelectionVisible();
        QVERIFY(QRectF(0, 0, 700, 600).contains(view.selectedScreenBounds()));
        d.setProperty("width", 1000.);
        view.ensureSelectionVisible();
        QVERIFY(QRectF(0, 0, 700, 600).contains(view.selectedScreenBounds()));
        d.toggleVisible(3);
        QVERIFY(!d.items()[3].visible);
        QVERIFY(!view.selectedScreenBounds().isEmpty());
    }
    void editingAndHistory() {
        Document d;
        QCOMPARE(d.rowCount(), 5);
        QVERIFY(!d.dirty());
        d.select(3);
        d.setProperty("text", "HALLO");
        QCOMPARE(d.selection()["text"].toString(), "HALLO");
        d.undo();
        QCOMPARE(d.selection()["name"].toString(), "MOIN");
        d.select(3);
        QCOMPARE(d.selection()["text"].toString(), "MOIN");
        d.redo();
        d.select(3);
        QCOMPARE(d.selection()["text"].toString(), "HALLO");
        d.addShape("ellipse");
        QCOMPARE(d.rowCount(), 6);
        d.setProperty("width", -1);
        QCOMPARE(d.selection()["width"].toDouble(), 50.);
        d.setProperty("width", 42.5);
        QCOMPARE(d.selection()["width"].toDouble(), 42.5);
        d.duplicateSelected();
        QCOMPARE(d.rowCount(), 7);
        d.removeSelected();
        QCOMPARE(d.rowCount(), 6);
        d.undo();
        QCOMPARE(d.rowCount(), 7);
    }
    void moveHistory() {
        Document d;
        d.beginMove();
        d.moveSelection({41, 25});
        d.moveSelection({42, 25});
        d.finishMove();
        QCOMPARE(d.selection()["x"].toDouble(), 42.);
        d.undo();
        QCOMPARE(d.selection()["x"].toDouble(), 30.);
        QVERIFY(!d.canUndo());
    }
    void persistence() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QUrl url = QUrl::fromLocalFile(dir.filePath("test.blazer"));
        Document d;
        d.setProperty("x", 21.5);
        d.setLayerSetting(1, "power", 35.5);
        QVERIFY(d.save(url));
        QVERIFY(!d.dirty());
        Document loaded;
        QVERIFY(loaded.open(url));
        QCOMPARE(loaded.selection()["x"].toDouble(), 21.5);
        QCOMPARE(loaded.layers()[1].toMap()["power"].toDouble(), 35.5);
        QCOMPARE(loaded.rowCount(), 5);
        QFile f(url.toLocalFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        auto arr = root["objects"].toArray();
        auto obj = arr[0].toObject();
        obj["width"] = -20;
        arr[0] = obj;
        root["objects"] = arr;
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();
        QVERIFY(!loaded.open(url));
        QCOMPARE(loaded.rowCount(), 5);
        QCOMPARE(loaded.selection()["width"].toDouble(), 140.);
    }
    void projectMigrationAndAtomicValidation() {
        QTemporaryDir dir;
        auto url = QUrl::fromLocalFile(dir.filePath("legacy.blazer"));
        Document d;
        QVERIFY(d.save(url));
        QFile file(url.toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto write = [&](const QJsonObject &r) {
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            const auto bytes = QJsonDocument(r).toJson();
            bool ok = file.write(bytes) == bytes.size();
            file.close();
            return ok;
        };
        auto legacy = root;
        legacy["version"] = 1;
        legacy.remove("material");
        auto objects = legacy["objects"].toArray();
        for (int i = 0; i < objects.size(); ++i) {
            auto obj = objects[i].toObject();
            obj["y"] = 150 - obj["y"].toDouble() - obj["height"].toDouble();
            objects[i] = obj;
        }
        legacy["objects"] = objects;
        QVERIFY(write(legacy));
        Document reopened;
        QVERIFY(reopened.open(url));
        QCOMPARE(reopened.items()[0].bounds, d.items()[0].bounds);
        for (int variant = 0; variant < 4; ++variant) {
            auto bad = root;
            auto arr = bad["objects"].toArray();
            auto obj = arr[0].toObject();
            if (variant == 0)
                obj["visible"] = "true";
            if (variant == 1)
                obj["x"] = "20";
            if (variant == 2) {
                auto duplicate = arr[1].toObject();
                duplicate["id"] = obj["id"];
                arr[1] = duplicate;
            }
            if (variant == 3)
                bad["origin"] = "top-left";
            arr[0] = obj;
            bad["objects"] = arr;
            QVERIFY(write(bad));
            QVERIFY(!reopened.open(url));
            QCOMPARE(reopened.items()[0].bounds, d.items()[0].bounds);
        }
    }
    void svgImport() {
        QTemporaryDir dir;
        auto url = QUrl::fromLocalFile(dir.filePath("shape.svg"));
        QFile f(url.toLocalFile());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns='http://www.w3.org/2000/svg' width='100mm' height='40mm' viewBox='0 0 "
                "100 40'><rect width='100' height='40'/></svg>");
        f.close();
        Document d;
        QVERIFY(d.importSvg(url));
        QCOMPARE(d.rowCount(), 6);
        QCOMPARE(d.selection()["width"].toDouble(), 100.);
        QCOMPARE(d.selection()["height"].toDouble(), 40.);
        QCOMPARE(d.selection()["x"].toDouble(), 50.);
        auto saved = QUrl::fromLocalFile(dir.filePath("svg.blazer"));
        QVERIFY(d.save(saved));
        Document loaded;
        QVERIFY(loaded.open(saved));
        QCOMPARE(loaded.items().last().asset->bytes, d.items().last().asset->bytes);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("<svg xmlns='http://www.w3.org/2000/svg'><image "
                "href='file:///tmp/private.png'/></svg>");
        f.close();
        QVERIFY(!d.importSvg(url));
        QCOMPARE(d.rowCount(), 6);
    }
    void operationValidation() {
        Document d;
        d.setLayerSetting(1, "power", 101);
        QCOMPARE(d.layers()[1].toMap()["power"].toDouble(), 80.);
        d.setLayerSetting(1, "passes", 1.5);
        QCOMPARE(d.layers()[1].toMap()["passes"].toInt(), 1);
        d.setLayerSetting(1, "speed", 0);
        QCOMPARE(d.layers()[1].toMap()["speed"].toDouble(), 600.);
    }
    void focusInterlocks() {
        Simulator s;
        s.focus(true);
        QVERIFY(s.firing());
        s.start();
        QVERIFY(!s.busy());
        s.jog(5, 5);
        QCOMPARE(s.x(), 100.);
        s.focus(false);
        QVERIFY(!s.firing());
        s.start();
        QVERIFY(s.busy());
        s.focus(true);
        QVERIFY(!s.firing());
        s.pause();
        QCOMPARE(s.state(), "Pausiert");
        s.focus(true);
        QVERIFY(!s.firing());
        s.stop();
        QVERIFY(!s.busy());
    }
    void focusTimeout() {
        Simulator s;
        s.focus(true);
        QVERIFY(s.firing());
        QTRY_VERIFY_WITH_TIMEOUT(!s.firing(), 3600);
    }
    void connectionFailure() {
        Simulator s;
        s.focus(true);
        s.disconnectFault();
        QVERIFY(!s.connected());
        QVERIFY(!s.firing());
        s.focus(true);
        QVERIFY(!s.firing());
        s.toggleConnection();
        QVERIFY(s.connected());
        QVERIFY(!s.firing());
        s.jog(-1000, 1000);
        QCOMPARE(s.x(), 0.);
        QCOMPARE(s.y(), 150.);
    }
};
QTEST_MAIN(CoreTest)
#include "core_test.moc"
