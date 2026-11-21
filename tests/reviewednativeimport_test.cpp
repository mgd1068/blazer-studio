#include "document.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ReviewedNativeImportTest : public QObject {
    Q_OBJECT
    static QUrl put(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size()) qFatal("fixture write failed");
        return QUrl::fromLocalFile(path);
    }
    static QByteArray rectangle() { return "<Shape Type='Rect' W='10' H='5' CutIndex='2'><XForm>1 0 0 1 20 20</XForm></Shape>"; }
    static QByteArray project(const QByteArray &shapes) {
        return "<LightBurnProject FormatVersion='1'><CutSetting type='Cut'><speed Value='50'/><maxPower Value='100'/><doOutput Value='0'/></CutSetting>"+shapes+"</LightBurnProject>";
    }
  private slots:
    void previewIsReadOnlyAndReviewedGeometryDoesNotActivateForeignOutput() {
        QTemporaryDir storage;
        const auto file = put(storage.filePath("foreign.lbrn2"),project(rectangle()));
        Document document; document.setBackgroundPlanning(false); document.newProject(); document.addShape("rect");
        document.setBasicOperation("outline");
        QVERIFY(document.save(QUrl::fromLocalFile(storage.filePath("destination.blazer"))));
        const auto before = document.projectBytes();
        const auto beforeCode = document.job().gcode();
        const auto destination = document.fileUrl();
        const auto preview = document.previewNativeImport(file);
        QVERIFY2(preview["geometryValidated"].toBool(),qPrintable(preview["error"].toString()));
        QVERIFY(preview["geometryOnlyRequired"].toBool());
        QCOMPARE(document.projectBytes(),before); QVERIFY(!document.dirty());
        QVERIFY(!document.importFile(file)); QCOMPARE(document.projectBytes(),before);
        QVERIFY(document.importNativeGeometry(file,preview["sourceSha256"].toString()));
        QCOMPARE(document.count(),2); QCOMPARE(document.fileUrl(),destination);
        QCOMPARE(document.items().last().toolLayer,1);
        QCOMPARE(document.job().gcode(),beforeCode); // Source doOutput=0 is never silently turned on.
        document.setBasicOperation("outline");
        QCOMPARE(document.items().last().toolLayer,0);
        QVERIFY(document.items().last().engraveOutline);
        QVERIFY(document.job().gcode()!=beforeCode);
        document.undo(); QCOMPARE(document.items().last().toolLayer,1);
        document.undo(); QCOMPARE(document.projectBytes(),before);
    }
    void staleReviewBadDigestAndUnsupportedGeometryAreAtomic() {
        QTemporaryDir storage;
        const auto path = storage.filePath("changing.lbrn2");
        auto file = put(path,project(rectangle()));
        Document document; document.setBackgroundPlanning(false); document.newProject();
        const auto before = document.projectBytes();
        const auto preview = document.previewNativeImport(file);
        QVERIFY(preview["geometryValidated"].toBool());
        put(path,project(rectangle()+rectangle()));
        QVERIFY(!document.importNativeGeometry(file,preview["sourceSha256"].toString()));
        QCOMPARE(document.projectBytes(),before);
        QVERIFY(!document.importNativeGeometry(file,QString(64,'a')+'\n'));
        QCOMPARE(document.projectBytes(),before);
        put(path,"<LightBurnProject MirrorX='True'>"+rectangle()+"</LightBurnProject>");
        const auto unsupported = document.previewNativeImport(file,true);
        QVERIFY(!unsupported["geometryValidated"].toBool()); QVERIFY(!unsupported["canImportGeometry"].toBool());
        QVERIFY(!document.importNativeGeometry(file,unsupported["sourceSha256"].toString(),true));
        QCOMPARE(document.projectBytes(),before);
        QVERIFY(!document.previewNativeImport(QUrl("https://invalid.test/private.lbrn2"))["geometryValidated"].toBool());
    }
    void fontsRequireIndependentPermissionAndRepeatedGroupsNeverMerge() {
        QTemporaryDir storage;
        const auto text = project("<Shape Type='Text' Str='Hello' Font='DejaVu Sans' H='5'><XForm>1 0 0 1 20 20</XForm></Shape>");
        auto file = put(storage.filePath("text.lbrn2"),text);
        Document document; document.setBackgroundPlanning(false); document.newProject();
        QVERIFY(!document.previewNativeImport(file)["geometryValidated"].toBool());
        QVERIFY(document.previewNativeImport(file,true)["geometryValidated"].toBool());
        const auto grouped = project("<Shape Type='Group'><XForm>1 0 0 1 0 0</XForm><Children>"+rectangle()+rectangle()+"</Children></Shape>");
        file = put(storage.filePath("group.lbrn2"),grouped);
        const auto preview = document.previewNativeImport(file);
        QVERIFY2(preview["geometryValidated"].toBool(),qPrintable(preview["error"].toString()));
        QVERIFY(document.importNativeGeometry(file,preview["sourceSha256"].toString()));
        const auto firstGroup = document.items()[0].groupId;
        QVERIFY(!firstGroup.isEmpty()); QCOMPARE(document.items()[1].groupId,firstGroup);
        QVERIFY(document.importNativeGeometry(file,preview["sourceSha256"].toString()));
        QVERIFY(document.items()[2].groupId!=firstGroup);
        QCOMPARE(document.items()[2].groupId,document.items()[3].groupId);
        QVERIFY(!document.job().valid()); // All four remain explicit non-output templates.
    }
};
QTEST_MAIN(ReviewedNativeImportTest)
#include "reviewednativeimport_test.moc"
