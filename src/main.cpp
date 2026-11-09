#include "document.h"
#include "simulator.h"
#include "workspace.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Blazer Studio");
    app.setOrganizationName("BlazerStudio");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
    Document document;
    Simulator simulator;
    QFile f(":/profiles/blaze-x-m3.json");
    if (!f.open(QIODevice::ReadOnly))
        return 1;
    auto profile = QJsonDocument::fromJson(f.readAll()).toVariant();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("project", &document);
    engine.rootContext()->setContextProperty("machine", &simulator);
    engine.rootContext()->setContextProperty("deviceProfile", profile);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("BlazerStudio", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto args = app.arguments();
    auto *root = engine.rootObjects().first();
    if (args.contains("--dark"))
        root->setProperty("night", true);
    int view = args.indexOf("--view");
    if (view >= 0 && view + 1 < args.size())
        root->setProperty("page", args[view + 1].toInt());
    int small = args.indexOf("--compact");
    if (small >= 0) {
        root->setProperty("width", 1180);
        root->setProperty("height", 760);
    }
    int imported = args.indexOf("--import");
    if (imported >= 0 && imported + 1 < args.size())
        document.importSvg(QUrl::fromLocalFile(args[imported + 1]));
    int shot = args.indexOf("--screenshot");
    if (shot >= 0 && shot + 1 < args.size()) {
        const QString path = args[shot + 1];
        QTimer::singleShot(1200, &app, [&engine, &app, path] {
            auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            bool ok = w && w->grabWindow().save(path);
            app.exit(ok ? 0 : 2);
        });
    }
    return app.exec();
}
