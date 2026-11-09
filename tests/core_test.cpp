#include "document.h"
#include "simulator.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class CoreTest : public QObject {
    Q_OBJECT
  private slots:
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
        QCOMPARE(loaded.items().last().svg, d.items().last().svg);
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
