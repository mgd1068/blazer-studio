#include "document.h"
#include "grbl.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2)
        return 2;
    GrblSession s;
    int stage = 0;
    const bool motion = argc > 2 && QByteArray(argv[2]) == "--motion";
    const bool auxiliaryOn = argc > 2 && QByteArray(argv[2]) == "--aux-on";
    const bool auxiliaryOff = argc > 2 && QByteArray(argv[2]) == "--aux-off";
    const bool dryJob = argc > 2 && QByteArray(argv[2]) == "--dry-job";
    QObject::connect(&s, &GrblSession::changed, &app, [&] {
        if (!s.ready())
            return;
        QTextStream(stdout) << "READY stage=" << stage << " X=" << s.position.x()
                            << " Y=" << s.position.y() << "\n";
        if ((auxiliaryOn || auxiliaryOff) && stage == 0) {
            stage = 1;
            QTextStream(stdout) << "AUXILIARY: " << (auxiliaryOn ? "M8" : "M9")
                                << " requested, laser disabled, no motion\n";
            s.commands({"M5 S0", auxiliaryOn ? "M8" : "M9", "$G"});
            return;
        }
        if (!motion && !dryJob) {
            s.close();
            app.exit(0);
            return;
        }
        if (stage == 0) {
            stage = 1;
            s.home();
        } else if (stage == 1) {
            stage = 2;
            if (dryJob) {
                Document d;
                d.newProject();
                d.addShape("rect");
                d.setProperty("x", 10.);
                d.setProperty("y", 10.);
                d.setProperty("width", 5.);
                d.setProperty("height", 5.);
                auto lines = d.job().gcode();
                // Dry-run: remove every laser-enable command, retaining exact geometry/feed.
                for (auto &line : lines)
                    if (line.startsWith("M4"))
                        line = "M5 S0";
                QTextStream(stdout) << "DRY JOB: 5mm contour, laser enable replaced with M5 S0\n";
                s.commands(lines);
            } else
                s.jog(5, 5);
        } else if (stage == 2) {
            stage = 3;
            if (dryJob)
                s.commands({"M5 S0", "G90 G54 G0 X0 Y0"});
            else
                s.jog(-5, -5);
        } else if (stage == 3) {
            stage = 4;
            s.commands({"M5 S0", "$G"});
        } else {
            s.close();
            app.exit(0);
        }
    });
    QTimer::singleShot(30000, &app, [&] {
        QTextStream(stderr) << s.state << "\n" << s.log.join("\n") << "\n";
        s.close();
        app.exit(3);
    });
    if (!s.open(QString::fromLocal8Bit(argv[1])))
        return 2;
    return app.exec();
}
