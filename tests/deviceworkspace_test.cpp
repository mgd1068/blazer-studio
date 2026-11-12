#include "document.h"
#include "machine.h"
#include "workspace.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

class DeviceWorkspaceTest : public QObject {
    Q_OBJECT
    DeviceProfile profile(double width = 600, double height = 400) {
        auto json = DeviceProfile::m3Reference().toJson();
        json["id"] = "test-workspace";
        json["name"] = "Synthetisches Profil";
        auto workspace = json["workspace"].toObject();
        workspace["width"] = width;
        workspace["height"] = height;
        json["workspace"] = workspace;
        auto driver = json["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        json["driver"] = driver;
        json["provenance"] = QJsonObject{{"privateNote", "must-not-be-saved"}};
        QString error;
        auto parsed = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
        if (!parsed)
            qFatal("Bad fixture");
        return *parsed;
    }
    QJsonObject root(const Document &document) {
        return QJsonDocument::fromJson(document.projectBytes()).object();
    }
  private slots:
    void projectProfileRoundTripPreservesPhysicalGeometryAndHistory() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto oldBytes = document.projectBytes();
        QCOMPARE(root(document)["version"].toInt(), 8);
        document.setProperty("x", 400.);
        document.setProperty("y", 300.);
        document.setProperty("width", 30.);
        document.setProperty("height", 20.);
        const auto bounds = document.items().first().bounds;
        QVERIFY(!document.job().valid());
        QVERIFY(document.setDeviceProfile(profile()));
        QCOMPARE(document.items().first().bounds, bounds);
        QVERIFY(document.material().isEmpty());
        QVERIFY(document.job().valid());
        QVERIFY(document.job().gcode().contains("M4 S204"));
        QCOMPARE(document.workArea(), QSizeF(600, 400));
        QCOMPARE(root(document)["version"].toInt(), 11);
        QVERIFY(!document.projectBytes().contains("must-not-be-saved"));
        const auto guide = document.addGuide("horizontal", 350);
        QVERIFY(!guide.isEmpty());
        const auto path = QUrl::fromLocalFile(storage.filePath("large.blazer"));
        QVERIFY(document.save(path));
        Document reopened;
        QVERIFY(reopened.open(path));
        QCOMPARE(reopened.workArea(), QSizeF(600, 400));
        QCOMPARE(reopened.items().first().bounds, bounds);
        QCOMPARE(reopened.job().gcode(), document.job().gcode());
        QCOMPARE(reopened.allGuides().size(), 1);
        QVERIFY(!reopened.dirty());
        Document recovered;
        QVERIFY(recovered.restoreBytes(document.projectBytes()));
        QCOMPARE(recovered.deviceProfile().outputFingerprint(),
                 document.deviceProfile().outputFingerprint());
        auto before = recovered.projectBytes();
        recovered.undo();
        QCOMPARE(recovered.workArea(), QSizeF(200, 150));
        recovered.redo();
        QCOMPARE(recovered.projectBytes(), before);
        const auto templ = QUrl::fromLocalFile(storage.filePath("large.blazertemplate"));
        QVERIFY(document.saveTemplate(templ));
        Document fromTemplate;
        QVERIFY(fromTemplate.newFromTemplate(templ));
        QCOMPARE(fromTemplate.workArea(), QSizeF(600, 400));
        QVERIFY(fromTemplate.restoreBytes(oldBytes));
        QCOMPARE(fromTemplate.workArea(), QSizeF(200, 150));
        QCOMPARE(fromTemplate.items().first().bounds, QRectF(65, 55, 50, 30));
        QCOMPARE(root(fromTemplate)["version"].toInt(), 8);
    }
    void malformedOrSecretBearingProfilesDoNotMutateProjects() {
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto original = document.projectBytes();
        auto config = profile().toJson();
        auto driver = config["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["accessToken"] = "never-store-this";
        driver["settings"] = settings;
        config["driver"] = driver;
        QString error;
        const auto secret = DeviceProfile::parse(QJsonDocument(config).toJson(), error);
        QVERIFY(secret);
        QVERIFY(!document.setDeviceProfile(*secret));
        QCOMPARE(document.projectBytes(), original);
        QVERIFY(!document.setDeviceProfile(profile(2001, 400)));
        QCOMPARE(document.projectBytes(), original);
        auto malformed = root(document);
        malformed["deviceProfile"] = profile().toJson(); // New data under old version.
        QVERIFY(!document.restoreBytes(QJsonDocument(malformed).toJson()));
        QCOMPARE(document.projectBytes(), original);
        QVERIFY(document.setDeviceProfile(profile()));
        auto good = root(document);
        const auto stable = document.projectBytes();
        good.remove("deviceProfile");
        QVERIFY(!document.restoreBytes(QJsonDocument(good).toJson()));
        QCOMPARE(document.projectBytes(), stable);
        good = root(document);
        good["deviceProfile"] = config;
        QVERIFY(!document.restoreBytes(QJsonDocument(good).toJson()));
        QCOMPARE(document.projectBytes(), stable);
    }
    void simulatorProfileChangesAreAtomicAndOldFramesCannotRun() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto oldPath = QUrl::fromLocalFile(storage.filePath("m3.blazer"));
        QVERIFY(document.save(oldPath));
        const auto oldBytes = document.projectBytes();
        QVERIFY(document.setDeviceProfile(profile()));
        document.setProperty("x", 400.);
        document.setProperty("y", 300.);
        Machine machine(&document, nullptr, Machine::HardwareAccess::Disabled);
        QCOMPARE(machine.x(), 300.);
        QCOMPARE(machine.y(), 200.);
        const auto oldFrame = document.framingBounds("selection");
        QVERIFY(oldFrame["valid"].toBool());
        const auto stable = document.projectBytes();
        const auto revision = document.historyRevision();
        const auto url = document.fileUrl();
        machine.start();
        QVERIFY(machine.busy());
        QVERIFY(!document.setDeviceProfile(DeviceProfile::m3Reference()));
        QCOMPARE(document.projectBytes(), stable);
        QVERIFY(!document.open(oldPath));
        QCOMPARE(document.fileUrl(), url);
        QCOMPARE(document.historyRevision(), revision);
        QVERIFY(!document.restoreBytes(oldBytes));
        QCOMPARE(document.projectBytes(), stable);
        QVERIFY(!document.restoreHistory(0, document.historyRevision()));
        QCOMPARE(document.historyRevision(), revision);
        machine.stop();
        QVERIFY(document.setDeviceProfile(DeviceProfile::m3Reference()));
        machine.frame(oldFrame);
        QVERIFY(!machine.busy());
        QCOMPARE(document.message(), QString("Rahmenziel gehört zu einem anderen Geräteprofil."));
        machine.focus(true);
        QVERIFY(machine.firing());
        QVERIFY(!document.setDeviceProfile(profile()));
        machine.focus(false);
        QVERIFY(document.setDeviceProfile(profile()));
        machine.jog(1000, 1000);
        QCOMPARE(machine.x(), 600.);
        QCOMPARE(machine.y(), 400.);
        machine.frame(document.framingBounds("selection"));
        QVERIFY(machine.busy());
        machine.stop();
        QVERIFY(!machine.connectHardware("this-is-not-a-device"));
        // A destroyed Machine must leave no dangling profile guard.
        Document other;
        {
            Machine temporary(&other, nullptr, Machine::HardwareAccess::Disabled);
        }
        QVERIFY(other.setDeviceProfile(profile()));
    }
    void renderingRegistrationAndSvgUseOnePhysicalArea() {
        QTemporaryDir storage;
        Document document;
        document.newProject();
        QVERIFY(document.setDeviceProfile(profile()));
        document.addShape("rect");
        document.setProperty("x", 400.);
        document.setProperty("y", 300.);
        document.setProperty("width", 30.);
        document.setProperty("height", 20.);
        Workspace workspace;
        workspace.setWidth(1000);
        workspace.setHeight(800);
        workspace.setDocument(&document);
        const double scale = 890. / 600;
        const QPointF origin(55, (800 - 400 * scale) / 2);
        const auto selected = workspace.selectedScreenBounds();
        QVERIFY(QLineF(selected.topLeft(), origin + QPointF(400, 80) * scale).length() < 1e-8);
        QVERIFY(std::abs(selected.width() - 30 * scale) < 1e-8);
        QVERIFY(std::abs(selected.height() - 20 * scale) < 1e-8);
        QImage rendered(1000, 800, QImage::Format_ARGB32);
        rendered.fill(Qt::transparent);
        QPainter painter(&rendered);
        workspace.paint(&painter);
        painter.end();
        QVERIFY(rendered.save(storage.filePath("large.png")));
        const auto svgPath = QUrl::fromLocalFile(storage.filePath("large.svg"));
        QVERIFY(document.exportSvg(svgPath));
        QFile svg(svgPath.toLocalFile());
        QVERIFY(svg.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(svg.readAll());
        QVERIFY(xml.readNextStartElement());
        QCOMPARE(xml.attributes().value("width").toString(), QString("600mm"));
        QCOMPARE(xml.attributes().value("height").toString(), QString("400mm"));
        QCOMPARE(xml.attributes().value("viewBox").toString(), QString("0 0 600 400"));
        Registration registration;
        workspace.setRegistration(&registration);
        QImage photo(400, 300, QImage::Format_RGB32);
        photo.fill(Qt::white);
        const auto photoPath = QUrl::fromLocalFile(storage.filePath("photo.png"));
        QVERIFY(photo.save(photoPath.toLocalFile()));
        QVERIFY(registration.load(photoPath));
        QVERIFY(registration.calibrate({0, 0, 400, 0, 400, 300, 0, 300},
                                       {0, 400, 600, 400, 600, 0, 0, 0}));
        QVERIFY(QLineF(registration.transform().map(QPointF(400, 300)), QPointF(600, 0)).length() <
                1e-8);
        auto renamed = profile().toJson();
        renamed["name"] = "Nur umbenannt";
        QString error;
        const auto same = DeviceProfile::parse(QJsonDocument(renamed).toJson(), error);
        QVERIFY(same);
        QVERIFY(document.setDeviceProfile(*same));
        QVERIFY(registration.calibrated());
        QVERIFY(document.setDeviceProfile(profile(100, 80)));
        QVERIFY(!registration.calibrated());
        QVERIFY(!registration.visible());
        QVERIFY(!registration.calibrate({0, 0, 400, 0, 400, 300, 0, 300},
                                        {0, 400, 600, 400, 600, 0, 0, 0}));
        QVERIFY(registration.calibrate({0, 0, 400, 0, 400, 300, 0, 300},
                                       {0, 80, 100, 80, 100, 0, 0, 0}));
    }
};
QTEST_MAIN(DeviceWorkspaceTest)
#include "deviceworkspace_test.moc"
