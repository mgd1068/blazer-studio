#include "grbl.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Do not retain a raw-command bypass around the session safety contract.
    // Reject legacy motion/auxiliary switches before even opening the endpoint.
    if (argc != 2) {
        QTextStream(stderr) << "Usage: blazer-grbl-probe PORT (connection diagnostics only).\n"
                            << "Motion and accessory probe modes are disabled pending verified "
                               "machine and output contracts.\n";
        return 2;
    }
    GrblSession s;
    QObject::connect(&s, &GrblSession::changed, &app, [&] {
        if (!s.ready())
            return;
        QTextStream(stdout) << "Connection verified; X=" << s.position.x()
                            << " Y=" << s.position.y() << "\n"
                            << s.motionSafetyError() << "\n";
        s.close();
        app.exit(0);
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
