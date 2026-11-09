#include "document.h"
#include "simulator.h"
#include "workspace.h"
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <functional>

class UiTest : public QObject {
    Q_OBJECT
  private slots:
    void editNavigateAndFocus() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
        Document project;
        Simulator machine;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("project", &project);
        engine.rootContext()->setContextProperty("machine", &machine);
        QFile f(QStringLiteral(BLAZER_SOURCE_DIR "/profiles/blaze-x-m3.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        engine.rootContext()->setContextProperty("deviceProfile",
                                                 QJsonDocument::fromJson(f.readAll()).toVariant());
        engine.load(QUrl::fromLocalFile(QStringLiteral(BLAZER_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(w);
        QVERIFY(QTest::qWaitForWindowExposed(w));
        auto item = [w](const char *name) {
            std::function<QQuickItem *(QQuickItem *)> visit =
                [&](QQuickItem *node) -> QQuickItem * {
                if (node->objectName() == QString::fromLatin1(name))
                    return node;
                for (auto *child : node->childItems())
                    if (auto *found = visit(child))
                        return found;
                return nullptr;
            };
            return visit(w->contentItem());
        };
        auto click = [w](QQuickItem *i) {
            QTest::mouseClick(w, Qt::LeftButton, {},
                              i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint());
        };
        auto *add = item("addRectangle");
        QVERIFY(add);
        click(add);
        QCOMPARE(project.rowCount(), 6);
        QVERIFY(project.dirty());
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(project.rowCount(), 5);
        auto *preview = item("workflow1");
        QVERIFY(preview);
        click(preview);
        QCOMPARE(w->property("page").toInt(), 1);
        auto *device = item("workflow2");
        QVERIFY(device);
        click(device);
        QCOMPARE(w->property("page").toInt(), 2);
        QTest::qWait(60);
        auto *focus = item("focusHold");
        QVERIFY(focus);
        auto point = focus->mapToScene(QPointF(focus->width() / 2, focus->height() / 2)).toPoint();
        QTest::mousePress(w, Qt::LeftButton, {}, point);
        QVERIFY(machine.firing());
        QTest::mouseRelease(w, Qt::LeftButton, {}, point);
        QVERIFY(!machine.firing());
        QTest::mousePress(w, Qt::LeftButton, {}, point);
        QVERIFY(machine.firing());
        w->setProperty("page", 0);
        QVERIFY(!machine.firing());
        QTest::mouseRelease(w, Qt::LeftButton, {}, point);
        w->setProperty("night", true);
        QTest::qWait(40);
        QVERIFY(!w->grabWindow().isNull());
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
