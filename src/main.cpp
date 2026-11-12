#include "applicationservices.h"
#include "depthservice.h"
#include "document.h"
#include "machine.h"
#include "materiallibrary.h"
#include "recovery.h"
#include "reliefpreview.h"
#include "workspace.h"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--version") {
            fprintf(stdout, "Blazer Studio %s\n", BLAZER_VERSION);
            return 0;
        }
    QGuiApplication app(argc, argv);
    app.setApplicationName("Blazer Studio");
    app.setOrganizationName("BlazerStudio");
    app.setApplicationVersion(QStringLiteral(BLAZER_VERSION));
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<Workspace>("BlazerStudio", 1, 0, "Workspace");
    qmlRegisterType<ReliefPreview>("BlazerStudio", 1, 0, "ReliefPreview");
    Document document;
    document.setBackgroundPlanning(!app.arguments().contains("--export-gcode"));
    Registration registration;
    QTemporaryDir transientRecovery;
    bool transient = app.arguments().contains("--screenshot") ||
                     app.arguments().contains("--export-gcode") ||
                     app.arguments().contains("--self-test");
    Recovery recovery(&document, transient ? transientRecovery.path() : QString());
    auto script = QStringLiteral(BLAZER_TOOLS_DIR "/photo_depth.py");
    if (!QFile::exists(script))
        script = QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath("../share/blazer-studio/tools/photo_depth.py");
    DepthService depthService(&document, script);
    if (!recovery.writable()) {
        QQmlApplicationEngine errorEngine;
        errorEngine.rootContext()->setContextProperty(
            "startupErrorMessage",
            "Die Anwendungsdaten sind bereits durch eine andere Instanz belegt oder nicht "
            "beschreibbar. Bitte das vorhandene Fenster verwenden oder die Zugriffsrechte prüfen.");
        errorEngine.loadFromModule("BlazerStudio", "StartupError");
        if (errorEngine.rootObjects().isEmpty())
            return 3;
        return app.exec();
    }
    MaterialLibrary materialLibrary(transient ? transientRecovery.filePath("materials.json")
                                             : QString());
    Machine simulator(&document, nullptr, transient ? Machine::HardwareAccess::Disabled
                                                  : Machine::HardwareAccess::Allowed);
    ApplicationServices services(&document, &simulator,
                                 transient ? transientRecovery.filePath("settings.ini") : QString());
    QFile f(":/profiles/blaze-x-m3.json");
    if (!f.open(QIODevice::ReadOnly))
        return 1;
    auto profile = QJsonDocument::fromJson(f.readAll()).toVariant();
    QFile materials(":/profiles/manufacturer-materials.json");
    if (!materials.open(QIODevice::ReadOnly))
        return 1;
    auto reference = QJsonDocument::fromJson(materials.readAll()).toVariant();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appServices", &services);
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
    // Desktop Exec supplies an optional positional .blazer file; an empty %f means normal startup.
    if (args.size() == 2 && !args[1].startsWith('-') &&
        !document.open(QUrl::fromLocalFile(args[1])))
        return 2;
    int opened = args.indexOf("--open-project");
    if (opened >= 0 &&
        (opened + 1 >= args.size() || !document.open(QUrl::fromLocalFile(args[opened + 1]))))
        return 2;
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
    if (args.contains("--self-test")) {
        // Runtime/package smoke test. Machine remains in simulation; never open a port.
        auto fail = [&app](const char *message) {
            fprintf(stderr, "SELF-TEST FAILED: %s\n", message);
            app.exit(2);
        };
        QTimer::singleShot(15000, &app, [fail] { fail("timeout"); });
        QTimer::singleShot(
            0, &app, [&document, &simulator, &services, &transientRecovery, &app, root, fail] {
                document.newProject();
                document.addShape("rect");
                QImage image(8, 8, QImage::Format_RGB32);
                image.fill(Qt::gray);
                const auto png = transientRecovery.filePath("sample.png");
                const auto jpg = transientRecovery.filePath("sample.jpg");
                QFile svg(transientRecovery.filePath("sample.svg"));
                if (!image.save(png) || !image.save(jpg) || QImage(jpg).isNull() ||
                    !svg.open(QIODevice::WriteOnly) ||
                    svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"5mm\" "
                              "height=\"5mm\" viewBox=\"0 0 5 5\"><rect width=\"4\" "
                              "height=\"4\"/></svg>") < 0) {
                    fail("image codecs or SVG fixture");
                    return;
                }
                svg.close();
                if (!document.importImage(QUrl::fromLocalFile(png)) ||
                    !document.importSvg(QUrl::fromLocalFile(svg.fileName())) ||
                    !QFontDatabase::families().contains("DejaVu Sans") || !simulator.simulation()) {
                    fail("imports, fonts or simulation default");
                    return;
                }
                const auto saved =
                    QUrl::fromLocalFile(transientRecovery.filePath("roundtrip.blazer"));
                if (!document.save(saved) || !document.open(saved) || document.count() != 3 ||
                    !services.exportDiagnostics(
                        QUrl::fromLocalFile(transientRecovery.filePath("diagnostics.json")))) {
                    fail("project roundtrip or diagnostics");
                    return;
                }
                auto *poll = new QTimer(&app);
                poll->setInterval(50);
                QObject::connect(
                    poll, &QTimer::timeout, &app,
                    [&document, &simulator, &services, &transientRecovery, &app, root, poll, fail] {
                        if (document.planningBusy())
                            return;
                        poll->stop();
                        auto *window = qobject_cast<QQuickWindow *>(root);
                        if (!document.job().valid() || !simulator.simulation() || !window ||
                            window->grabWindow().isNull() ||
                            !document.exportGcode(
                                QUrl::fromLocalFile(transientRecovery.filePath("sample.gcode")))) {
                            fail("job planning, rendering or GCode export");
                            return;
                        }
                        const auto output =
                            QJsonDocument(QJsonObject{{"selfTest", "passed"},
                                                      {"version", services.version()},
                                                      {"revision", services.revision()},
                                                      {"simulation", true},
                                                      {"objects", document.count()},
                                                      {"hardwareTested", false}})
                                .toJson();
                        fwrite(output.constData(), 1, output.size(), stdout);
                        app.exit(0);
                    });
                poll->start();
            });
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
