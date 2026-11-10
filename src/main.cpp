#include "depthservice.h"
#include "document.h"
#include "machine.h"
#include "materiallibrary.h"
#include "recovery.h"
#include "reliefpreview.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("Blazer Studio");
    app.setOrganizationName("BlazerStudio");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
    qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    Document document;
    Registration registration;
    QTemporaryDir transientRecovery;
    bool transient =
        app.arguments().contains("--screenshot") || app.arguments().contains("--export-gcode");
    Recovery recovery(&document, transient ? transientRecovery.path() : QString());
    auto script = QStringLiteral(BLAZER_TOOLS_DIR "/photo_depth.py");
    if (!QFile::exists(script))
        script = QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath("../share/blazer-studio/tools/photo_depth.py");
    DepthService depthService(&document, script);
    MaterialLibrary materialLibrary;
    Machine simulator(&document);
    QFile f(":/profiles/blaze-x-m3.json");
    if (!f.open(QIODevice::ReadOnly))
        return 1;
    auto profile = QJsonDocument::fromJson(f.readAll()).toVariant();
    QFile materials(":/profiles/manufacturer-materials.json");
    if (!materials.open(QIODevice::ReadOnly))
        return 1;
    auto reference = QJsonDocument::fromJson(materials.readAll()).toVariant();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("project", &document);
    engine.rootContext()->setContextProperty("recovery", &recovery);
    engine.rootContext()->setContextProperty("depthService", &depthService);
    engine.rootContext()->setContextProperty("cameraRegistration", &registration);
    engine.rootContext()->setContextProperty("materialLibrary", &materialLibrary);
    engine.rootContext()->setContextProperty("machine", &simulator);
    engine.rootContext()->setContextProperty("deviceProfile", profile);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.rootContext()->setContextProperty("manufacturerReference", reference);
    engine.loadFromModule("BlazerStudio", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto args = app.arguments();
    auto *root = engine.rootObjects().first();
    if (args.contains("--new"))
        document.newProject();
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
    int toolsSection = args.indexOf("--tools-section");
    if (toolsSection >= 0 && toolsSection + 1 < args.size())
        if (auto *tools = root->findChild<QObject *>("toolsPanel"))
            tools->setProperty("section", args[toolsSection + 1].toInt());
    int generic = args.indexOf("--import-file");
    if (generic >= 0 && generic + 1 < args.size() &&
        !document.importFile(QUrl::fromLocalFile(args[generic + 1])))
        return 2;
    int imported = args.indexOf("--import");
    if (imported >= 0 && imported + 1 < args.size())
        document.importSvg(QUrl::fromLocalFile(args[imported + 1]));
    if (args.contains("--material-test"))
        document.createMaterialTest(1000, 3000, 10, 50, 4, 5);
    int bitmap = args.indexOf("--import-image");
    if (bitmap >= 0 && bitmap + 1 < args.size())
        document.importImage(QUrl::fromLocalFile(args[bitmap + 1]));
    int relief = args.indexOf("--import-relief");
    if (relief >= 0 && relief + 1 < args.size() &&
        !document.importRelief(QUrl::fromLocalFile(args[relief + 1])))
        return 2;
    int exported = args.indexOf("--export-gcode");
    if (exported >= 0 && exported + 1 < args.size()) {
        bool ok = document.exportGcode(QUrl::fromLocalFile(args[exported + 1]));
        if (!ok)
            return 2;
        if (!args.contains("--screenshot"))
            return 0;
    }
    if (args.contains("--materials"))
        QMetaObject::invokeMethod(root, "showManufacturerReference");
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
