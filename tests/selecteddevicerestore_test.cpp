#include "devicemanager.h"
#include "document.h"
#include "machine.h"
#include "controllercalibrationstore.h"
#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFile>
#include <QDir>

class SelectedDeviceRestoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    int sequence=0;
    QString libraryPath;
    const QString preferenceKey="devices/lastSelected/v1";
    static DeviceProfile profile(int width) {
        auto json=DeviceProfile::m3Reference().toJson();auto area=json["workspace"].toObject();
        area["width"]=width;json["workspace"]=area;QString error;
        const auto result=DeviceProfile::parse(QJsonDocument(json).toJson(),error);
        if(!result)qFatal("invalid fixture profile");return *result;
    }
    QString remembered(int width=160) {
        Document document;document.setBackgroundPlanning(false);document.newProject();
        if(!document.setDeviceProfile(profile(width)))return {};
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);
        DeviceManager manager(&document,&machine,libraryPath);
        const auto id=manager.addCurrent("Synthetic remembered laser","/synthetic/not-a-device");
        if(id.isEmpty()||!manager.activate(id))return {};
        return id;
    }
    bool calibrate(const QString &id,int width=160,bool wrongProfile=false) {
        DeviceLibrary library(libraryPath);const auto binding=library.bindingKey(id);
        QVariantMap record{{"schema",1},{"binding",binding},{"deviceIdentityHash",QString(64,'a')},
            {"profileFingerprint",wrongProfile?QString(64,'d'):QString::fromLatin1(profile(width).outputFingerprint())},
            {"settingsFingerprint",QString(64,'b')},{"homeX",0.},{"homeY",0.},
            {"minX",0.},{"minY",0.},{"maxX",double(width-1)},{"maxY",149.},
            {"observedMinX",0.},{"observedMinY",0.},{"observedMaxX",double(width-1)},{"observedMaxY",149.},
            {"workOriginX",0.},{"workOriginY",0.}};
        QString error;return ControllerCalibrationStore().save(binding.toLatin1(),record,&error);
    }
    static void disconnected(Machine &machine) {
        QVERIFY(machine.simulation());QCOMPARE(machine.connectionPhase(),QString("closed"));
        QVERIFY(!machine.connectionVerified());QVERIFY(!machine.occupied());
        QVERIFY(!machine.requestPending());QVERIFY(!machine.firing());
    }
private slots:
    void init() {
        QVERIFY(storage.isValid());const auto directory=storage.filePath(QString::number(++sequence));QVERIFY(QDir().mkpath(directory));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory);
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,directory+"/system");
        QCoreApplication::setOrganizationName("BlazerSelectionRestoreTest");
        QCoreApplication::setApplicationName("case"+QString::number(sequence));
        qputenv("XDG_DATA_HOME",(directory+"/data").toUtf8());
        libraryPath=directory+"/devices.json";
    }
    void missingPreferenceMigratesOnlyAuthenticatedConfiguredLaser() {
        const auto id=remembered();QVERIFY(!id.isEmpty());QVERIFY(calibrate(id));
        {QSettings settings;settings.remove(preferenceKey);settings.sync();QVERIFY(!settings.contains(preferenceKey));}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QCOMPARE(manager.activeId(),id);QCOMPARE(document.bedWidth(),160.);disconnected(machine);
        QSettings settings;QCOMPARE(settings.value(preferenceKey).toMap().value("instanceId").toString(),id);
        QVERIFY(!manager.autoHomeOnConnect());
    }
    void missingPreferenceWithoutMatchingCalibrationDoesNotGuess_data() {
        QTest::addColumn<bool>("wrongProfile");QTest::newRow("no-calibration")<<false;QTest::newRow("different-calibration-profile")<<true;
    }
    void missingPreferenceWithoutMatchingCalibrationDoesNotGuess() {
        QFETCH(bool,wrongProfile);const auto id=remembered();QVERIFY(!id.isEmpty());if(wrongProfile)QVERIFY(calibrate(id,160,true));
        {QSettings settings;settings.remove(preferenceKey);settings.sync();}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QVERIFY(manager.activeId().isEmpty());QCOMPARE(document.bedWidth(),200.);disconnected(machine);
        QSettings settings;QVERIFY(!settings.contains(preferenceKey));
    }
    void multipleAuthenticatedCandidatesDoNotGuess() {
        const auto first=remembered();QVERIFY(!first.isEmpty());QString second;
        {Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);second=manager.addCurrent("Other configured laser",{});QVERIFY(!second.isEmpty());}
        QVERIFY(calibrate(first));QVERIFY(calibrate(second));
        {QSettings settings;settings.remove(preferenceKey);settings.sync();}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QVERIFY(manager.activeId().isEmpty());disconnected(machine);QSettings settings;QVERIFY(!settings.contains(preferenceKey));
    }
    void restoresExactLocalIdentityAndProfileWithoutConnect() {
        const auto id=remembered();QVERIFY(!id.isEmpty());QString binding;
        {QSettings settings;const auto saved=settings.value(preferenceKey).toMap();QCOMPARE(saved.value("instanceId").toString(),id);binding=saved.value("binding").toString();QCOMPARE(binding.size(),64);}
        Document document;document.setBackgroundPlanning(false);document.newProject();QCOMPARE(document.bedWidth(),200.);
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QCOMPARE(manager.activeId(),id);QCOMPARE(manager.bindingKey(),binding);QCOMPARE(document.bedWidth(),160.);disconnected(machine);
        QVERIFY(!manager.autoHomeOnConnect());
    }
    void backgroundStartupPlanningDoesNotLoseRememberedDevice() {
        const auto id=remembered();QVERIFY(!id.isEmpty());Document document;document.setBackgroundPlanning(true);
        Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QCOMPARE(manager.activeId(),id);QCOMPARE(document.bedWidth(),160.);disconnected(machine);
        QTRY_VERIFY(!document.planningBusy());
    }
    void explicitSwitchReplacesRememberedIdentity() {
        const auto old=remembered();QVERIFY(!old.isEmpty());QString selected;
        {Document document;document.setBackgroundPlanning(false);Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
         QCOMPARE(manager.activeId(),old);selected=manager.addCurrent("Second synthetic laser",{});QVERIFY(!selected.isEmpty());QVERIFY(manager.activate(selected));disconnected(machine);}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QCOMPARE(manager.activeId(),selected);disconnected(machine);
    }
    void staleBindingRevisionNeverRestores() {
        const auto id=remembered();QVERIFY(!id.isEmpty());
        {DeviceLibrary library(libraryPath);QVERIFY(library.update(id,"Changed endpoint",profile(160),"/synthetic/other"));}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QVERIFY(manager.activeId().isEmpty());QVERIFY(manager.bindingKey().isEmpty());QCOMPARE(document.bedWidth(),200.);disconnected(machine);
    }
    void invalidPreference_data(){QTest::addColumn<QString>("invalid");for(const auto name:{"schema","binding","missing-id","extra-field","string-map"})QTest::newRow(name)<<QString::fromLatin1(name);}
    void invalidPreference() {
        QFETCH(QString,invalid);const auto id=remembered();QVERIFY(!id.isEmpty());QVERIFY(calibrate(id));
        {QSettings settings;auto value=settings.value(preferenceKey).toMap();
         if(invalid=="schema")value["schema"]=2;
         if(invalid=="binding")value["binding"]=QString(64,'d');
         if(invalid=="missing-id")value["instanceId"]="nonexistent";
         if(invalid=="extra-field")value["connected"]=true;
         settings.setValue(preferenceKey,invalid=="string-map"?QVariant("invalid"):QVariant(value));settings.sync();}
        Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        QVERIFY(manager.activeId().isEmpty());disconnected(machine);
    }
    void malformedSettingsSelectionCannotRewriteExistingBytes() {
        const auto id=remembered();QVERIFY(!id.isEmpty());QString path;{QSettings settings;path=settings.fileName();}
        const QByteArray invalid("[General]\nretained=value\n[broken\n");
        {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));QCOMPARE(file.write(invalid),invalid.size());}
        {Document document;Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
         QVERIFY(manager.activeId().isEmpty());QVERIFY(manager.activate(id));disconnected(machine);}
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),invalid);
    }
    void fileProfileMetadataCannotSwitchChosenHardwareOrScaleGeometry() {
        const auto url=QUrl::fromLocalFile(storage.filePath("larger-project.blazer"));
        {Document source;source.setBackgroundPlanning(false);source.newProject();QVERIFY(source.setDeviceProfile(profile(300)));source.addShape("rect");source.setProperty("x",250.);source.setProperty("y",20.);source.setProperty("width",10.);source.setProperty("height",5.);QVERIFY(source.save(url));}
        const auto id=remembered();QVERIFY(!id.isEmpty());
        Document document;document.setBackgroundPlanning(false);Machine machine(&document,nullptr,Machine::HardwareAccess::Disabled);DeviceManager manager(&document,&machine,libraryPath);
        const auto binding=manager.bindingKey();QVERIFY(document.open(url));
        QCOMPARE(manager.activeId(),id);QCOMPARE(manager.bindingKey(),binding);QCOMPARE(document.bedWidth(),160.);
        QCOMPARE(document.items().size(),1);QCOMPARE(document.items().first().bounds.x(),250.);QCOMPARE(document.items().first().bounds.width(),10.);
        QVERIFY(!document.job().valid());disconnected(machine);
        document.select(0);document.setProperty("x",240.);document.undo();
        QCOMPARE(document.bedWidth(),160.);QCOMPARE(manager.activeId(),id);
        QCOMPARE(document.items().first().bounds.x(),250.);
        document.redo();QCOMPARE(document.bedWidth(),160.);
        // Removing the physical selection removes its runtime-only preference.
        QVERIFY(manager.remove(id));QVERIFY(manager.activeId().isEmpty());
        QVERIFY(document.open(url));QCOMPARE(document.bedWidth(),300.);disconnected(machine);
    }
};
QTEST_MAIN(SelectedDeviceRestoreTest)
#include "selecteddevicerestore_test.moc"
