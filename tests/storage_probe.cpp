#include "document.h"
#include "recovery.h"
#include <QGuiApplication>
#include <csignal>
#include <cstdio>
#include <sys/resource.h>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    auto args = app.arguments();
    if (args.size() != 3)
        return 2;
    Document d;
    d.newProject();
    d.addShape("rect");
    d.setProperty("width", 37.);
    if (args[1] == "--quota") {
        std::signal(SIGXFSZ, SIG_IGN);
        rlimit limit{256, 256};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
            return 3;
        return d.save(QUrl::fromLocalFile(args[2])) ? 4 : 0;
    }
    Recovery recovery(&d, args[2]);
    if (!recovery.writable() || !recovery.save())
        return 5;
    std::puts("ready");
    std::fflush(stdout);
    return app.exec();
}
