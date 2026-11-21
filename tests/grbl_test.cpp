#include "document.h"
#include "grbl.h"
#include "machine.h"
#include <QJsonDocument>
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSocketNotifier>
#include <QtTest>
#include <fcntl.h>
#include <limits>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
class Controller : public QObject {
  public:
    int master = -1;
    QString port;
    QByteArray buffer;
    QStringList commands;
    QString status = "Idle";
    bool banner = false, wrongPower = false, autoAck = true, withOverrides = false;
    bool respondToStatus = true, respondToModal = true, interleaveSettingsOnce = false;
    bool interleaveSettingsAlways = false;
    QByteArray minimumPower = "0";
    bool respondToReset = true, holdBlocksCommands = false, rejectHandshakeSafetyOff = false;
    QString resetState = "Idle";
    int settingsRequests = 0, statusQueries = 0;
    QSocketNotifier *notifier = nullptr;
    QByteArray realtime;
    int feed = 100, power = 100;
    QPointF machinePosition, workOffset;
    Controller() {
        int slave;
        char name[256];
        if (openpty(&master, &slave, name, nullptr, nullptr))
            qFatal("openpty failed");
        port = name;
        termios attributes{};
        if (tcgetattr(slave, &attributes))
            qFatal("tcgetattr failed");
        cfmakeraw(&attributes);
        if (tcsetattr(slave, TCSANOW, &attributes))
            qFatal("tcsetattr failed");
        close(slave);
        fcntl(master, F_SETFL, O_NONBLOCK);
        notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] {
            char data[4096];
            ssize_t n;
            while ((n = read(master, data, sizeof(data))) > 0)
                for (int i = 0; i < n; ++i) {
                    char c = data[i];
                    unsigned char byte = static_cast<unsigned char>(c);
                    if (byte >= 0x80) {
                        realtime.append(c);
                        switch (byte) {
                        case 0x90:
                            feed = 100;
                            break;
                        case 0x91:
                            feed += 10;
                            break;
                        case 0x92:
                            feed -= 10;
                            break;
                        case 0x93:
                            ++feed;
                            break;
                        case 0x94:
                            --feed;
                            break;
                        case 0x99:
                            power = 100;
                            break;
                        case 0x9a:
                            power += 10;
                            break;
                        case 0x9b:
                            power -= 10;
                            break;
                        case 0x9c:
                            ++power;
                            break;
                        case 0x9d:
                            --power;
                            break;
                        }
                        feed = qBound(10, feed, 200);
                        power = qBound(10, power, 200);
                        continue;
                    }
                    if (c == '?') {
                        ++statusQueries;
                        if (respondToStatus)
                            report();
                        continue;
                    }
                    if (c == 0x18) {
                        realtime.append(c);
                        buffer.clear();
                        status = resetState;
                        banner = true;
                        if (respondToReset)
                            reply("Grbl 1.3a ['$' for help]\r\n");
                        continue;
                    }
                    if (c == '!') {
                        realtime.append(c);
                        status = "Hold";
                        continue;
                    }
                    if (c == '~') {
                        realtime.append(c);
                        status = "Run";
                        continue;
                    }
                    if (c == '\n' || c == '\r') {
                        if (!banner) {
                            banner = true;
                            reply("Grbl 1.3a ['$' for help]\r\n");
                            buffer.clear();
                            continue;
                        }
                        if (buffer.isEmpty())
                            continue;
                        auto line = QString::fromLatin1(buffer);
                        buffer.clear();
                        commands << line;
                        if (holdBlocksCommands && status == "Hold")
                            continue;
                        if (line == "G10 L20 P1 X0 Y0")
                            workOffset = machinePosition;
                        if (line == "$I")
                            reply("[VER:1.3a.20211103:]\r\n");
                        if (line == "$G" && respondToModal)
                            reply("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\n");
                        if (line == "$$") {
                            ++settingsRequests;
                            QByteArray settings = wrongPower ? "$30=255\r\n" : "$30=1000\r\n";
                            if (interleaveSettingsAlways ||
                                (interleaveSettingsOnce && settingsRequests == 1))
                                settings = "$30=1000<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0>\r\n";
                            reply(settings + "$31=" + minimumPower +
                                  "\r\n$32=1\r\n$22=1\r\n$13=0\r\n"
                                  "$130=200\r\n$131=150\r\n");
                        }
                        if (autoAck)
                            reply(rejectHandshakeSafetyOff && line == "M5 S0" ? "error:9\r\n"
                                                                              : "ok\r\n");
                    } else
                        buffer += c;
                }
        });
    }
    ~Controller() {
        if (master >= 0)
            close(master);
    }
    void disconnectPort() {
        notifier->setEnabled(false);
        close(master);
        master = -1;
    }
    void reply(const QByteArray &s) {
        if (master >= 0)
            QVERIFY(write(master, s.constData(), s.size()) == s.size());
    }
    void report() {
        reply(("<" + status + "|MPos:" + QString::number(machinePosition.x()) + "," +
               QString::number(machinePosition.y()) + ",0|WCO:" + QString::number(workOffset.x()) +
               "," + QString::number(workOffset.y()) + ",0|FS:0,0" +
               (withOverrides ? "|Ov:" + QString::number(feed) + ",100," + QString::number(power)
                              : QString()) +
               ">\r\n")
                  .toLatin1());
    }
};
class GrblTest : public QObject {
    Q_OBJECT
    void supplySyntheticContract(GrblSession &session) {
        // This is an independent fixture assumption, never a real-machine claim.
        session.commissionedEnvelope = QRectF(0, 0, 200, 150);
        session.commissionedModalContract = true;
        session.commissionedHomingContract = true;
        session.commissionedOverrideContract = true;
    }
    void requireReference(ControllerSession &session) {
        auto grbl = dynamic_cast<GrblSession *>(&session);
        if (!session.status().positionReferenced) {
            if (grbl)
                grbl->commissionedHomingContract = true;
            session.home();
            QTRY_VERIFY(session.status().ready && session.status().positionReferenced);
        }
        if (grbl)
            supplySyntheticContract(*grbl);
    }
    void requireReference(Machine &machine) {
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        if (!machine.positionReferenced()) {
            grbl->commissionedHomingContract = true;
            machine.home();
            QTRY_VERIFY(machine.ready() && machine.positionReferenced());
        }
        supplySyntheticContract(*grbl);
        machine.accessory = AccessoryContract(true);
        QVERIFY(machine.accessory.configure(
            AccessoryContract::Role::EnclosureExtraction,
            {"synthetic-pty-extraction", "virtual fixture only, no hardware", true}));
        const auto request =
            machine.accessory.request(AccessoryContract::Role::EnclosureExtraction, true);
        QVERIFY(request);
        QVERIFY(machine.accessory.acknowledge(*request, true));
        const auto now = machine.accessoryClock.elapsed();
        QVERIFY(machine.accessory.observe(AccessoryContract::Role::EnclosureExtraction,
                                          AccessoryContract::PhysicalState::Flowing,
                                          "virtual airflow fixture", now, now));
    }
    const QStringList workZeroCommands{"M5 S0", "G21", "G90", "G10 L20 P1 X0 Y0", "G54", "$G"};
    void acknowledgeWorkZero(Controller &controller, GrblSession &session, int cut,
                             bool acknowledgeLast = true) {
        for (int i = 0; i < workZeroCommands.size(); ++i) {
            QTRY_COMPARE(controller.commands.value(cut + i), workZeroCommands[i]);
            if (i + 1 == workZeroCommands.size() && !acknowledgeLast)
                break;
            controller.reply("ok\r\n");
        }
        if (acknowledgeLast)
            QTRY_VERIFY(session.workZero && session.workZero->queryArmed);
        else
            QTRY_VERIFY(session.workZero && session.workZero->modalReadback);
    }
  private slots:
    void actualMachineRegistrationBlocksHeavyDocumentSideEffects_data() {
        QTest::addColumn<QString>("activity");
        for (const auto &activity : {"job", "paused-job", "jog", "focus", "frame", "home", "zero",
                                     "reset", "handshake", "lease-with-lost-flags"})
            QTest::newRow(activity) << QString(activity);
    }
    void actualMachineRegistrationBlocksHeavyDocumentSideEffects() {
        QFETCH(QString, activity);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto target = QUrl::fromLocalFile(dir.filePath("sentinel.blazer"));
        QFile output(target.toLocalFile());
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write("unchanged"), qint64(9));
        output.close();
        const auto svg = QUrl::fromLocalFile(dir.filePath("import.svg"));
        QFile input(svg.toLocalFile());
        QVERIFY(input.open(QIODevice::WriteOnly));
        QVERIFY(input.write("<svg xmlns='http://www.w3.org/2000/svg' width='20mm' height='20mm' "
                            "viewBox='0 0 20 20'><rect x='1' y='2' width='3' height='4'/></svg>") >
                0);
        input.close();
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        const auto before = document.projectBytes();
        QVERIFY(!before.isEmpty());
        const auto history = document.historyPosition();
        const auto count = document.items().size();
        const auto fileUrl = document.fileUrl();
        QSignalSpy guardChanges(&document, &Document::synchronousWorkGuardChanged);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto cut = controller.commands.size();
        const auto initialResets = controller.realtime.count(char(0x18));
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        if (activity == "job" || activity == "paused-job" || activity == "lease-with-lost-flags")
            machine.start();
        else if (activity == "jog")
            machine.jog(1, 0);
        else if (activity == "focus")
            machine.focus(true);
        else if (activity == "frame")
            machine.frame(document.framingBounds("job"));
        else if (activity == "home")
            machine.home();
        else if (activity == "zero")
            machine.setWorkZero();
        else if (activity == "reset" || activity == "handshake") {
            machine.stop();
            QTRY_COMPARE(controller.realtime.count(char(0x18)), initialResets + 1);
            if (activity == "handshake") {
                controller.reply("Grbl 1.3a ['$' for help]\r\n");
                QTRY_VERIFY(grbl->handshaking);
            }
        }
        if (activity == "paused-job")
            machine.pause();
        if (activity == "lease-with-lost-flags") {
            QVERIFY(machine.accessory.activeLease());
            machine.accessoryJobActive = machine.accessoryFocusActive = false;
            for (int i = 0; i < 80 && machine.busy(); ++i) {
                if (grbl->waitingAck)
                    controller.reply("ok\r\n");
                if (grbl->waitingIdle)
                    controller.report();
                QTest::qWait(2);
            }
            QTRY_VERIFY(machine.ready());
            QVERIFY(machine.accessory.activeLease());
            QVERIFY(!machine.accessoryJobActive && !machine.accessoryFocusActive);
        }
        QVERIFY(!document.save(target));
        // Observe the actual Machine signal registration; no fake guard was supplied.
        QVERIFY(!guardChanges.isEmpty());
        QVERIFY(!document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("unchanged"));
        output.close();
        QVERIFY(!document.importFile(svg));
        auto clipboard = QGuiApplication::clipboard();
        clipboard->setText("offline-machine-guard-sentinel");
        QVERIFY(!document.copySelectionToClipboard());
        QCOMPARE(clipboard->text(), QString("offline-machine-guard-sentinel"));
        const QString pasteSvg =
            "<svg xmlns='http://www.w3.org/2000/svg' width='10mm' height='10mm' viewBox='0 0 10 "
            "10'><rect width='3' height='4'/></svg>";
        clipboard->setText(pasteSvg);
        QVERIFY(!document.pasteFromClipboard());
        QCOMPARE(clipboard->text(), pasteSvg);
        QVERIFY(document.projectBytes().isEmpty());
        QCOMPARE(document.items().size(), count);
        QCOMPARE(document.historyPosition(), history);
        QCOMPARE(document.fileUrl(), fileUrl);
        const auto blockedSignals = guardChanges.size();
        if (activity == "zero") {
            acknowledgeWorkZero(controller, *grbl, cut);
            controller.report();
            QTRY_VERIFY(machine.ready());
            QCOMPARE(controller.realtime.count(char(0x18)), initialResets);
        } else {
            if (activity != "reset")
                machine.stop(); // Reset row already requested its stop before these guarded calls.
            QVERIFY(
                !document.synchronousWorkBlockReason().isEmpty()); // Reset-pending is still active.
            const int expectedResets = initialResets + ((activity == "handshake") ? 2 : 1);
            QTRY_COMPARE(controller.realtime.count(char(0x18)), expectedResets);
            controller.autoAck = controller.respondToStatus = true;
            controller.reply("Grbl 1.3a ['$' for help]\r\n");
            QTRY_VERIFY(machine.ready());
        }
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(guardChanges.size() > blockedSignals);
        QCOMPARE(document.projectBytes(), before);
        QVERIFY(document.save(target));
        QVERIFY(
            document.importFile(svg)); // The same valid fixture is admitted after physical idle.
        QVERIFY(document.pasteFromClipboard());
        clipboard->clear();
    }
    void lightNextDesignEditsDoNotChangeTheAlreadyStreamingJob() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        const auto expected = document.job().gcode();
        QVERIFY(!expected.isEmpty());
        controller.autoAck = controller.respondToStatus = false;
        const auto cut = controller.commands.size();
        QSignalSpy guardChanges(&document, &Document::synchronousWorkGuardChanged);
        machine.start();
        QVERIFY(!document.synchronousWorkBlockReason().isEmpty());
        document.setProperty("x", 42.);
        document.setProperty("width", 25.);
        document.setProperty("name", QString("next design"));
        QCOMPARE(document.items().front().bounds.x(), 42.);
        QCOMPARE(document.items().front().bounds.width(), 25.);
        QCOMPARE(document.items().front().name, QString("next design"));
        QVERIFY(document.job().gcode() != expected);
        QVERIFY(machine.busy() && machine.accessory.activeLease());
        QVERIFY(!document.synchronousWorkBlockReason().isEmpty());
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        for (int i = 0; i < 80 && machine.busy(); ++i) {
            if (grbl->waitingAck)
                controller.reply("ok\r\n");
            if (grbl->waitingIdle)
                controller.report();
            QTest::qWait(2);
        }
        QTRY_VERIFY(machine.ready());
        QCOMPARE(controller.commands.mid(cut), expected);
        QVERIFY(!machine.accessory.activeLease());
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(!document.projectBytes().isEmpty());
        QVERIFY(guardChanges.size() > 1);
    }
    void simulatorActivityNeverActivatesThePhysicalSynchronousGuard_data() {
        QTest::addColumn<QString>("activity");
        QTest::newRow("job") << QString("job");
        QTest::newRow("focus") << QString("focus");
        QTest::newRow("frame") << QString("frame");
    }
    void simulatorActivityNeverActivatesThePhysicalSynchronousGuard() {
        QFETCH(QString, activity);
        QTemporaryDir dir;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.simulation() && machine.ready());
        if (activity == "job")
            machine.start();
        else if (activity == "focus")
            machine.focus(true);
        else
            machine.frame(document.framingBounds("job"));
        QVERIFY(machine.busy() || machine.firing());
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(!document.projectBytes().isEmpty());
        QVERIFY(document.save(QUrl::fromLocalFile(dir.filePath("simulator.blazer"))));
        machine.stop();
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
    }
    void destroyedPhysicalMachineReleasesTheActualDocumentGuardAndNotifications() {
        QTemporaryDir dir;
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        auto machine = std::make_unique<Machine>(&document);
        QPointer<Machine> owner = machine.get();
        QVERIFY(machine->connectHardware(controller.port));
        QTRY_VERIFY(machine->ready());
        requireReference(*machine);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        machine->start();
        QVERIFY(!document.synchronousWorkBlockReason().isEmpty());
        QSignalSpy guardChanges(&document, &Document::synchronousWorkGuardChanged);
        machine.reset();
        QVERIFY(owner.isNull());
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(!guardChanges.isEmpty());
        bool lateQuery = false;
        QTimer::singleShot(80, &document,
                           [&] { lateQuery = document.synchronousWorkBlockReason().isEmpty(); });
        QTest::qWait(250); // Cross old watchdog/poll callbacks after owner teardown.
        QVERIFY(lateQuery);
        QVERIFY(document.synchronousWorkBlockReason().isEmpty());
        QVERIFY(!document.projectBytes().isEmpty());
        QVERIFY(document.save(QUrl::fromLocalFile(dir.filePath("after-owner.blazer"))));
    }
    void machineWorkZeroSurvivesDelayedAcknowledgementsWithoutFaultingFlow_data() {
        QTest::addColumn<QPointF>("machinePoint");
        QTest::addColumn<QPointF>("oldOffset");
        QTest::addColumn<QRectF>("envelope");
        QTest::newRow("shifted-positive")
            << QPointF(100, 75) << QPointF(10, -5) << QRectF(0, 0, 200, 150);
        QTest::newRow("signed-negative")
            << QPointF(-120, -40) << QPointF(-10, 5) << QRectF(-200, -150, 200, 150);
    }
    void machineWorkZeroSurvivesDelayedAcknowledgementsWithoutFaultingFlow() {
        QFETCH(QPointF, machinePoint);
        QFETCH(QPointF, oldOffset);
        QFETCH(QRectF, envelope);
        Controller controller;
        controller.machinePosition = machinePoint;
        controller.workOffset = oldOffset;
        Document document;
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        grbl->commissionedEnvelope = envelope;
        QVERIFY(!machine.accessory.activeLease());
        QVERIFY(machine.extractionSafetyError().isEmpty());
        controller.autoAck = controller.respondToStatus = false;
        const auto resets = controller.realtime.count(char(0x18));
        const auto cut = controller.commands.size();
        machine.setWorkZero();
        QVERIFY(grbl->status().coordinateUpdatePending);
        QTest::qWait(
            100); // The actual Machine watchdog runs twice while the first ACK is withheld.
        QVERIFY(grbl->status().coordinateUpdatePending);
        QVERIFY(machine.connected() && machine.positionReferenced());
        QVERIFY(!machine.running() && !machine.paused());
        QVERIFY(machine.extractionSafetyError().isEmpty());
        QVERIFY(!machine.accessory.activeLease());
        acknowledgeWorkZero(controller, *grbl, cut, false);
        QTest::qWait(80); // Last command is ACK-pending, still inside the bounded phase.
        QVERIFY(grbl->status().coordinateUpdatePending);
        controller.reply("ok\r\n");
        QTRY_VERIFY(grbl->workZero && grbl->workZero->queryArmed);
        QTest::qWait(80); // Readback waits across another watchdog interval.
        QVERIFY(grbl->status().coordinateUpdatePending);
        controller.report();
        QTRY_VERIFY(machine.ready());
        QVERIFY(!grbl->status().coordinateUpdatePending);
        QVERIFY(machine.connectionVerified() && machine.positionReferenced());
        QVERIFY(machine.extractionSafetyError().isEmpty());
        QVERIFY(!machine.accessory.activeLease());
        QCOMPARE(machine.x(), 0.);
        QCOMPARE(machine.y(), 0.);
        QCOMPARE(grbl->reportedMachinePosition, machinePoint);
        QCOMPARE(controller.machinePosition, machinePoint);
        QCOMPARE(controller.commands.mid(cut), workZeroCommands);
        QCOMPARE(controller.realtime.count(char(0x18)), resets);
    }
    void machineWorkZeroEntryDenialPreservesTheExistingOperation_data() {
        QTest::addColumn<QString>("operation");
        for (const auto &operation :
             {"actual-lease-only", "job-flag", "focus-flag", "streaming-job", "focus-pulse"})
            QTest::newRow(operation) << QString(operation);
    }
    void machineWorkZeroEntryDenialPreservesTheExistingOperation() {
        QFETCH(QString, operation);
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        if (operation == "actual-lease-only")
            QVERIFY(machine.accessory.begin(machine.accessoryClock.elapsed()));
        else if (operation == "job-flag")
            machine.accessoryJobActive = true;
        else if (operation == "focus-flag")
            machine.accessoryFocusActive = true;
        else if (operation == "streaming-job")
            machine.start();
        else if (operation == "focus-pulse")
            machine.focus(true);
        QTest::qWait(20); // Drain already accepted bytes of the existing operation.
        const auto lease = machine.accessory.activeLease();
        const auto jobFlag = machine.accessoryJobActive;
        const auto focusFlag = machine.accessoryFocusActive;
        const auto active = machine.busy();
        const auto pulse = machine.firing();
        const auto commands = controller.commands;
        const auto resets = controller.realtime.count(char(0x18));
        machine.setWorkZero();
        QTest::qWait(30);
        QCOMPARE(controller.commands, commands);
        QCOMPARE(controller.realtime.count(char(0x18)), resets);
        QCOMPARE(machine.accessory.activeLease(), lease);
        QCOMPARE(machine.accessoryJobActive, jobFlag);
        QCOMPARE(machine.accessoryFocusActive, focusFlag);
        QCOMPARE(machine.busy(), active);
        QCOMPARE(machine.firing(), pulse);
        QVERIFY(machine.positionReferenced());
        QVERIFY(!machine.session->status().coordinateUpdatePending);
    }
    void machineWorkZeroNeverExemptsAnOutputLeaseOrItsLostFlags_data() {
        QTest::addColumn<QString>("output");
        for (const auto &output : {"lease-only", "lease-during-zero", "job-flag", "focus-flag"})
            QTest::newRow(output) << QString(output);
    }
    void machineWorkZeroNeverExemptsAnOutputLeaseOrItsLostFlags() {
        QFETCH(QString, output);
        Controller controller;
        Document document;
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto resets = controller.realtime.count(char(0x18));
        const auto cut = controller.commands.size();
        if (output == "lease-only")
            QVERIFY(machine.accessory.begin(machine.accessoryClock.elapsed()));
        if (output == "job-flag")
            machine.accessoryJobActive = true;
        if (output == "focus-flag")
            machine.accessoryFocusActive = true;
        if (output == "lease-only") {
            QVERIFY(machine.accessory.activeLease());
            QVERIFY(!machine.accessoryJobActive && !machine.accessoryFocusActive);
        }
        grbl->zero(); // Deliberately bypass facade entry denial to exercise its runtime guard.
        if (output == "lease-during-zero") {
            QVERIFY(grbl->status().coordinateUpdatePending);
            QVERIFY(machine.accessory.begin(machine.accessoryClock.elapsed()));
            QVERIFY(machine.accessory.activeLease());
            QVERIFY(!machine.accessoryJobActive && !machine.accessoryFocusActive);
        }
        QTest::qWait(160);
        QVERIFY(!grbl->status().coordinateUpdatePending);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!machine.accessory.activeLease());
        QVERIFY(!machine.extractionSafetyError().isEmpty());
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(!controller.commands.mid(cut).contains("G10 L20 P1 X0 Y0"));
        QVERIFY(!controller.realtime.contains('~'));
    }
    void machineNormalRunCannotHideLostEvidenceBehindMissingOutputFlags_data() {
        QTest::addColumn<bool>("falseCoordinatePhase");
        QTest::newRow("ordinary-run") << false;
        QTest::newRow("mislabelled-coordinate-phase") << true;
    }
    void machineNormalRunCannotHideLostEvidenceBehindMissingOutputFlags() {
        QFETCH(bool, falseCoordinatePhase);
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        machine.start();
        QVERIFY(machine.accessory.activeLease());
        QTest::qWait(20); // Drain the accepted initial RX window before injecting evidence loss.
        const auto cut = controller.commands.size();
        const auto resets = controller.realtime.count(char(0x18));
        machine.accessoryJobActive = machine.accessoryFocusActive =
            false; // Deliberate facade flag loss.
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        if (falseCoordinatePhase)
            grbl->workZero =
                GrblSession::WorkZeroTransaction{grbl->reportedMachinePosition, grbl->offset};
        controller.reply("<Run|MPos:bad,0,0|WCO:0,0,0|FS:0,0>\r\n");
        QTest::qWait(80);
        controller.reply("ok\r\nok\r\nok\r\n"); // Further credits must not continue the old job.
        QTest::qWait(80);
        QVERIFY(!machine.accessory.activeLease());
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!grbl->status().coordinateUpdatePending);
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        for (const auto &line : controller.commands.mid(cut))
            QVERIFY(!line.startsWith("G1 ") && !line.startsWith("M4"));
        QVERIFY(!controller.realtime.contains('~'));
    }
    void machineWorkZeroFailuresLoseTheExceptionAndNeverResume_data() {
        QTest::addColumn<QString>("failure");
        for (const auto &failure :
             {"controller-error", "deadline", "user-stop", "unexpected-startup"})
            QTest::newRow(failure) << QString(failure);
    }
    void machineWorkZeroFailuresLoseTheExceptionAndNeverResume() {
        QFETCH(QString, failure);
        Controller controller;
        controller.machinePosition = {100, 75};
        controller.workOffset = {10, -5};
        Document document;
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto resets = controller.realtime.count(char(0x18));
        const auto cut = controller.commands.size();
        machine.setWorkZero();
        QVERIFY(grbl->status().coordinateUpdatePending);
        if (failure == "controller-error")
            controller.reply("error:20\r\n");
        else if (failure == "unexpected-startup")
            controller.reply("Grbl 1.3a ['$' for help]\r\n");
        else if (failure == "deadline")
            grbl->workZeroDeadline.start(80);
        else if (failure == "user-stop")
            machine.stop();
        QTest::qWait(180);
        QVERIFY(!grbl->status().coordinateUpdatePending);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!machine.accessory.activeLease());
        QVERIFY(!machine.extractionSafetyError().isEmpty());
        QVERIFY(!machine.accessoryJobActive && !machine.accessoryFocusActive);
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(!controller.commands.mid(cut).contains("G10 L20 P1 X0 Y0"));
        QVERIFY(!controller.realtime.contains('~'));
    }
    void zeroCannotTrustAChangedMachinePointEvenWhenWorkReportsZero() {
        Controller controller;
        controller.machinePosition = {100, 75};
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.respondToStatus = controller.respondToReset = false;
        session.zero();
        QTRY_VERIFY(session.waitingIdle);
        controller.machinePosition = controller.workOffset = {101, 75};
        controller.report(); // A self-consistent WPos0 must not conceal this movement.
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().positionReferenced);
    }
    void workZeroBlocksOtherActionsAndExplicitStopCancelsItsEpoch() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        session.zero();
        QTRY_VERIFY(controller.commands.last() == "M5 S0");
        const auto cut = controller.commands.size();
        const auto rtCut = controller.realtime.size();
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        session.start(document.job());
        session.jog(1, 0);
        session.frame({{"valid", true}, {"x", 0}, {"y", 0}, {"width", 10}, {"height", 10}});
        session.focus(true);
        session.home();
        session.pause();
        session.zero();
        QVERIFY(!session.overrides(125, 70));
        QTest::qWait(40);
        QCOMPARE(controller.commands.size(), cut);
        QCOMPARE(controller.realtime.mid(rtCut), QByteArray());
        QVERIFY(session.status().coordinateUpdatePending);
        session.stop();
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(session.status().resetting);
        controller.reply("ok\r\n<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0>\r\n");
        QTest::qWait(40);
        QVERIFY(session.status().resetting);
        QVERIFY(!session.status().positionReferenced);
        QVERIFY(!session.ready());
    }
    void workZeroRequiresItsModalReplyBeforeTheFinalAcknowledgement() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        controller.respondToModal = false;
        const auto cut = controller.commands.size();
        session.zero();
        for (int i = 0; i < workZeroCommands.size(); ++i) {
            QTRY_COMPARE(controller.commands.value(cut + i), workZeroCommands[i]);
            controller.reply("ok\r\n");
        }
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
    }
    void workZeroRejectsWrongOrAmbiguousFinalModalReadback_data() {
        QTest::addColumn<QByteArray>("modal");
        QTest::newRow("units-inches") << QByteArray("[GC:G0 G54 G17 G20 G90 G94 M5 T0 F0 S0]");
        QTest::newRow("relative") << QByteArray("[GC:G0 G54 G17 G21 G91 G94 M5 T0 F0 S0]");
        QTest::newRow("foreign-wcs") << QByteArray("[GC:G0 G55 G17 G21 G90 G94 M5 T0 F0 S0]");
        QTest::newRow("duplicate-wcs") << QByteArray("[GC:G0 G54 G55 G17 G21 G90 G94 M5 T0 F0 S0]");
        QTest::newRow("duplicate-units")
            << QByteArray("[GC:G0 G54 G17 G21 G20 G90 G94 M5 T0 F0 S0]");
        QTest::newRow("missing-distance") << QByteArray("[GC:G0 G54 G17 G21 G94 M5 T0 F0 S0]");
    }
    void workZeroRejectsWrongOrAmbiguousFinalModalReadback() {
        QFETCH(QByteArray, modal);
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToModal = controller.respondToStatus =
            controller.respondToReset = false;
        const auto cut = controller.commands.size();
        session.zero();
        for (int i = 0; i < workZeroCommands.size(); ++i) {
            QTRY_COMPARE(controller.commands.value(cut + i), workZeroCommands[i]);
            if (i + 1 < workZeroCommands.size())
                controller.reply("ok\r\n");
        }
        controller.reply(modal + "\r\nok\r\n");
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
    }
    void workZeroDoesNotSpliceAnOldPartialReportAcrossItsQueryBoundary() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto cut = controller.commands.size();
        session.zero();
        acknowledgeWorkZero(controller, session, cut, false);
        controller.reply("ok\r\n<Idle|MPos:0");
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
    }
    void stoppedWorkZeroCannotArmItsQueuedOldReadbackCallback() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        session.poll.stop(); // Isolate the queued transaction query from periodic diagnostics.
        const auto cut = controller.commands.size();
        session.zero();
        acknowledgeWorkZero(controller, session, cut, false);
        const auto queries = controller.statusQueries;
        bool cancel = true;
        connect(&session, &GrblSession::changed, &session, [&] {
            if (cancel && session.workZero &&
                session.workZero->phase == GrblSession::WorkZeroTransaction::Phase::Readback) {
                cancel = false;
                session.stop();
            }
        });
        controller.reply("ok\r\n");
        QTest::qWait(50);
        QVERIFY(!cancel);
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(session.status().resetting);
        QVERIFY(!session.status().positionReferenced);
        QCOMPARE(controller.statusQueries, queries);
    }
    void workZeroPreservesRawMachinePointAndCommitsOnlyAfterReadback_data() {
        QTest::addColumn<QPointF>("machine");
        QTest::addColumn<QPointF>("oldOffset");
        QTest::addColumn<QRectF>("envelope");
        QTest::addColumn<bool>("workOnly");
        QTest::newRow("shifted-positive")
            << QPointF(100, 75) << QPointF(10, -5) << QRectF(0, 0, 200, 150) << false;
        QTest::newRow("signed-negative")
            << QPointF(-120, -40) << QPointF(-10, 5) << QRectF(-200, -150, 200, 150) << false;
        QTest::newRow("work-plus-offset")
            << QPointF(25, 10) << QPointF(5, -2) << QRectF(0, 0, 200, 150) << true;
    }
    void workZeroPreservesRawMachinePointAndCommitsOnlyAfterReadback() {
        QFETCH(QPointF, machine);
        QFETCH(QPointF, oldOffset);
        QFETCH(QRectF, envelope);
        QFETCH(bool, workOnly);
        Controller controller;
        controller.machinePosition = machine;
        controller.workOffset = oldOffset;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        session.commissionedEnvelope = envelope;
        controller.autoAck = controller.respondToStatus = false;
        const auto cut = controller.commands.size();
        session.zero();
        QVERIFY(session.status().coordinateUpdatePending);
        QVERIFY(!session.status().verified && !session.ready());
        QTest::qWait(80); // Longer than the facade's safety watchdog cadence.
        QVERIFY(session.status().coordinateUpdatePending);
        acknowledgeWorkZero(controller, session, cut);
        QVERIFY(session.status().coordinateUpdatePending);
        QVERIFY(session.status().positionReferenced);
        if (workOnly)
            controller.reply("<Idle|WPos:0,0,0|WCO:25,10,0|FS:0,0>\r\n");
        else
            controller.report();
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(session.status().verified && session.status().positionReferenced);
        QCOMPARE(session.position, QPointF());
        QCOMPARE(session.reportedMachinePosition, machine);
        QCOMPARE(session.offset, machine);
        QCOMPARE(controller.machinePosition, machine);
        QCOMPARE(controller.commands.mid(cut), workZeroCommands);
    }
    void workZeroNeverCommitsAnIdleInTheFinalAckBatch() {
        Controller controller;
        controller.machinePosition = controller.workOffset = {100, 75}; // Already zero.
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = false;
        const auto cut = controller.commands.size();
        session.zero();
        acknowledgeWorkZero(controller, session, cut, false);
        controller.reply("ok\r\n<Idle|MPos:100,75,0|WCO:100,75,0|FS:0,0>\r\n");
        QTRY_VERIFY(session.workZero && session.workZero->queryArmed);
        QVERIFY(!session.ready());
        QVERIFY(session.status().coordinateUpdatePending);
        QVERIFY(!session.knownPosition && !session.knownOffset);
        controller.report();
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.status().coordinateUpdatePending);
    }
    void workZeroRejectsUnexpectedEvidenceAndNeverRetries_data() {
        QTest::addColumn<QByteArray>("reply");
        QTest::addColumn<bool>("readback");
        QTest::newRow("raw-movement-small")
            << QByteArray("<Idle|MPos:100.0001,75,0|WCO:10,-5,0|FS:0,0>") << false;
        QTest::newRow("wrong-offset-before-g10")
            << QByteArray("<Idle|MPos:100,75,0|WCO:100,75,0|FS:0,0>") << false;
        QTest::newRow("unexpected-offset")
            << QByteArray("<Idle|MPos:100,75,0|WCO:11,-5,0|FS:0,0>") << false;
        QTest::newRow("run") << QByteArray("<Run|MPos:100,75,0|WCO:10,-5,0|FS:0,0>") << false;
        QTest::newRow("hold") << QByteArray("<Hold:0|MPos:100,75,0|WCO:10,-5,0|FS:0,0>") << false;
        QTest::newRow("output") << QByteArray("<Idle|MPos:100,75,0|WCO:10,-5,0|FS:0,5>") << false;
        QTest::newRow("invalid-position")
            << QByteArray("<Idle|MPos:nan,75,0|WCO:10,-5,0|FS:0,0>") << false;
        QTest::newRow("units") << QByteArray("[GC:G0 G54 G17 G20 G90 G94 M5 T0 F0 S0]") << false;
        QTest::newRow("settings-epoch") << QByteArray("$13=0") << false;
        QTest::newRow("firmware-epoch") << QByteArray("[VER:1.3a.changed:]") << false;
        QTest::newRow("startup") << QByteArray("Grbl 1.3a ['$' for help]") << false;
        QTest::newRow("malformed") << QByteArray("garbage") << false;
        QTest::newRow("error") << QByteArray("error:9") << false;
        QTest::newRow("alarm") << QByteArray("ALARM:1") << false;
        QTest::newRow("missing-wco") << QByteArray("<Idle|MPos:100,75,0|FS:0,0>") << true;
        QTest::newRow("wrong-work-position")
            << QByteArray("<Idle|WPos:1,0,0|WCO:100,75,0|FS:0,0>") << true;
        QTest::newRow("stale-old-offset")
            << QByteArray("<Idle|MPos:100,75,0|WCO:10,-5,0|FS:0,0>") << true;
        QTest::newRow("unexpected-extra-ack") << QByteArray("ok") << true;
    }
    void workZeroRejectsUnexpectedEvidenceAndNeverRetries() {
        QFETCH(QByteArray, reply);
        QFETCH(bool, readback);
        Controller controller;
        controller.machinePosition = {100, 75};
        controller.workOffset = {10, -5};
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto cut = controller.commands.size();
        session.zero();
        QVERIFY(session.status().coordinateUpdatePending);
        if (readback)
            acknowledgeWorkZero(controller, session, cut);
        controller.reply(reply + "\r\n<Idle|MPos:100,75,0|WCO:100,75,0|FS:0,0>\r\nok\r\n");
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
        QVERIFY(!session.commissionedModalContract);
        session.zero();
        session.home();
        QTest::qWait(30);
        QVERIFY(controller.commands.mid(cut).count("G10 L20 P1 X0 Y0") <= 1);
        QVERIFY(!controller.commands.mid(cut).contains("$H"));
        QVERIFY(controller.realtime.contains(char(0x18)));
    }
    void workZeroMissingAckOrReadbackHasAnAbsoluteDeadline_data() {
        QTest::addColumn<bool>("allAcks");
        QTest::newRow("missing-ack") << false;
        QTest::newRow("missing-fresh-readback") << true;
    }
    void workZeroMissingAckOrReadbackHasAnAbsoluteDeadline() {
        QFETCH(bool, allAcks);
        Controller controller;
        controller.machinePosition = {100, 75};
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        const auto cut = controller.commands.size();
        session.zero();
        if (allAcks)
            acknowledgeWorkZero(controller, session, cut);
        session.workZeroDeadline.start(120); // Bounded fixture clock, not a production API.
        for (int i = 0; i < 4 && session.connected(); ++i) {
            if (!allAcks)
                controller.report(); // Valid old Idle traffic cannot replace an ACK.
            QTest::qWait(40);
        }
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
        QVERIFY(controller.commands.mid(cut).count("G10 L20 P1 X0 Y0") <= 1);
    }
    void workZeroCannotOverrideAnActiveMotionOrOutputAction_data() {
        QTest::addColumn<QString>("action");
        for (const auto &action : {"job", "frame", "jog", "focus", "pause", "home", "reset"})
            QTest::newRow(action) << QString(action);
    }
    void workZeroCannotOverrideAnActiveMotionOrOutputAction() {
        QFETCH(QString, action);
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = controller.respondToStatus = controller.respondToReset = false;
        if (action == "job") {
            Document document;
            document.setBackgroundPlanning(false);
            document.newProject();
            document.addShape("rect");
            session.start(document.job());
            QVERIFY(session.busy());
        } else if (action == "frame")
            session.frame({{"valid", true}, {"x", 0}, {"y", 0}, {"width", 10}, {"height", 10}});
        else if (action == "jog")
            session.jog(1, 0);
        else if (action == "focus")
            session.focus(true);
        else if (action == "pause")
            session.paused = true;
        else if (action == "home")
            session.home();
        else if (action == "reset")
            session.stop();
        const auto before = controller.commands.count("G10 L20 P1 X0 Y0");
        session.zero();
        QTest::qWait(30);
        QCOMPARE(controller.commands.count("G10 L20 P1 X0 Y0"), before);
        QVERIFY(!session.status().coordinateUpdatePending);
    }
    void workZeroWaitsForOverrideReadbackAndCancelsOnDisconnect() {
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.respondToStatus = false;
        QVERIFY(session.overrides(125, 70));
        QTest::qWait(20);
        QVERIFY(session.overrideReadbackPending);
        session.zero();
        QVERIFY(!session.status().coordinateUpdatePending);
        controller.report();
        QTRY_VERIFY(!session.overrideReadbackPending);
        controller.autoAck = false;
        session.zero();
        QVERIFY(session.status().coordinateUpdatePending);
        controller.disconnectPort();
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().coordinateUpdatePending);
        QVERIFY(!session.status().positionReferenced);
    }
    void repeatedOverridesRemainBoundedWithDelayedOrOldFeedback_data() {
        QTest::addColumn<int>("initialFeed");
        QTest::addColumn<int>("initialPower");
        QTest::addColumn<int>("targetFeed");
        QTest::addColumn<int>("targetPower");
        QTest::newRow("repeated-increase") << 70 << 70 << 190 << 95;
        QTest::newRow("repeated-reduction") << 100 << 100 << 10 << 10;
        QTest::newRow("reset-target") << 200 << 10 << 100 << 100;
        QTest::newRow("low-feed-low-power") << 10 << 10 << 10 << 10;
    }
    void repeatedOverridesRemainBoundedWithDelayedOrOldFeedback() {
        QFETCH(int, initialFeed);
        QFETCH(int, initialPower);
        QFETCH(int, targetFeed);
        QFETCH(int, targetPower);
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.feed = initialFeed;
        controller.power = initialPower;
        controller.report();
        QTRY_COMPARE(session.feedOverride, initialFeed);
        QTRY_COMPARE(session.powerOverride, initialPower);
        controller.respondToStatus = false;
        QTest::qWait(10);
        const auto cut = controller.realtime.size();
        for (int request = 0; request < 4; ++request) {
            QVERIFY(session.overrides(targetFeed, targetPower));
            // A delayed old report is indistinguishable from new Ov evidence.
            controller.reply(QByteArray("<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0|Ov:") +
                             QByteArray::number(initialFeed) + ",100," +
                             QByteArray::number(initialPower) + ">\r\n");
            QTest::qWait(10);
        }
        QCOMPARE(controller.power, targetPower);
        QCOMPARE(controller.feed, targetFeed);
        // Independent byte interpreter: inspect EVERY intermediate requested
        // percentage, rather than echoing the final encoder target.
        int interpretedFeed = initialFeed, interpretedPower = initialPower;
        const double energyRatioBound =
            qMax(1., qMax(double(initialPower) / initialFeed, double(targetPower) / targetFeed));
        for (unsigned char byte : controller.realtime.mid(cut)) {
            if (byte >= 0x90 && byte <= 0x94)
                QCOMPARE(interpretedPower, 10); // Lower power BEFORE changing feed.
            if (byte == 0x9a || byte == 0x9c)
                QCOMPARE(interpretedFeed, targetFeed); // Restore power AFTER feed settles.
            switch (byte) {
            case 0x90:
                interpretedFeed = 100;
                break;
            case 0x91:
                interpretedFeed += 10;
                break;
            case 0x92:
                interpretedFeed -= 10;
                break;
            case 0x93:
                ++interpretedFeed;
                break;
            case 0x94:
                --interpretedFeed;
                break;
            case 0x99:
                interpretedPower = 100;
                break;
            case 0x9a:
                interpretedPower += 10;
                break;
            case 0x9b:
                interpretedPower -= 10;
                break;
            case 0x9c:
                ++interpretedPower;
                break;
            case 0x9d:
                --interpretedPower;
                break;
            default:
                QFAIL("Unexpected realtime byte in override dispatch");
            }
            interpretedFeed = qBound(10, interpretedFeed, 200);
            interpretedPower = qBound(10, interpretedPower, 200);
            QVERIFY(interpretedFeed >= 10 && interpretedFeed <= 200);
            QVERIFY(interpretedPower >= 10 && interpretedPower <= qMax(initialPower, targetPower));
            QVERIFY(double(interpretedPower) / interpretedFeed <= energyRatioBound + 1e-9);
        }
        QCOMPARE(interpretedFeed, targetFeed);
        QCOMPARE(interpretedPower, targetPower);
    }
    void coalescedRealtimeModelMustNotInheritSequentialOverrideContract() {
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.power = 70;
        controller.report();
        QTRY_COMPARE(session.powerOverride, 70);
        controller.respondToStatus = false;
        QTest::qWait(10);
        const auto cut = controller.realtime.size();
        QVERIFY(session.overrides(100, 95)); // Explicit sequential virtual fixture only.
        QTRY_COMPARE(controller.power, 95);
        // Alternate bounded scheduler: contiguous minus flags coalesce before
        // servicing, while subsequent increase flags are individually serviced.
        // This is a counterexample, NOT the connected fixture's declared law.
        int coalescedPower = 70;
        bool previousMinus = false;
        for (unsigned char byte : controller.realtime.mid(cut)) {
            if (byte == 0x9b && !previousMinus)
                coalescedPower = qMax(10, coalescedPower - 10);
            if (byte == 0x9a)
                coalescedPower = qMin(200, coalescedPower + 10);
            if (byte == 0x9c)
                coalescedPower = qMin(200, coalescedPower + 1);
            previousMinus = byte == 0x9b;
        }
        QVERIFY(coalescedPower > 100); // Clamps alone cannot prove this protocol safe.
        session.commissionedOverrideContract = false;
        controller.report(); // Even fresh Ov is no commissioning evidence.
        QTest::qWait(10);
        const auto refusedCut = controller.realtime.size();
        QVERIFY(!session.overrides(100, 95));
        QTest::qWait(20);
        QCOMPARE(controller.realtime.mid(refusedCut), QByteArray());
        QVERIFY(!session.status().overridesReady);
    }
    void overrideContractCannotSurviveControllerEvidenceEpochChange_data() {
        QTest::addColumn<QString>("boundary");
        for (const auto &boundary : {"stop", "fault", "disconnect", "startup", "alarm", "modal"})
            QTest::newRow(boundary) << QString(boundary);
    }
    void overrideContractCannotSurviveControllerEvidenceEpochChange() {
        QFETCH(QString, boundary);
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        QVERIFY(session.status().overridesReady);
        controller.respondToReset = false;
        if (boundary == "stop")
            session.stop();
        else if (boundary == "disconnect")
            session.close();
        else if (boundary == "startup")
            controller.reply("Grbl 1.3a ['$' for help]\r\n");
        else if (boundary == "fault")
            controller.reply("<Idle|MPos:0,0,0|WPos:1,0,0|WCO:0,0,0|FS:0,0>\r\n");
        else if (boundary == "alarm") {
            controller.status = "Alarm";
            controller.report();
        } else if (boundary == "modal")
            controller.reply("[GC:G0 G55 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\n");
        QTRY_VERIFY(!session.commissionedOverrideContract);
        QVERIFY(!session.status().overridesReady);
        QVERIFY(!session.status().overridesSafetyError.isEmpty());
        session.close();
        Controller replacement;
        QVERIFY(session.open(replacement.port));
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.commissionedOverrideContract);
        QVERIFY(!session.status().overridesReady);
    }
    void overrideDispatchRejectsMissingOrAmbiguousMotionEvidence_data() {
        QTest::addColumn<QString>("loss");
        for (const auto &loss : {"envelope",       "modal",          "override-contract",
                                 "firing",         "reference",      "stale",
                                 "stale-feedback", "offset",         "position",
                                 "output",         "configuration",  "verification",
                                 "feedback",       "feedback-arity", "feedback-rapid",
                                 "reset",          "homing",         "handshake",
                                 "state",          "bounds",         "inputs"})
            QTest::newRow(loss) << QString(loss);
    }
    void overrideDispatchRejectsMissingOrAmbiguousMotionEvidence() {
        QFETCH(QString, loss);
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        QTRY_VERIFY(session.overrideFeedback);
        controller.respondToStatus = false;
        if (loss == "envelope")
            session.commissionedEnvelope.reset();
        else if (loss == "modal")
            session.commissionedModalContract = false;
        else if (loss == "override-contract")
            session.commissionedOverrideContract = false;
        else if (loss == "firing")
            session.firing = true;
        else if (loss == "reference")
            session.positionReferenced = false;
        else if (loss == "stale")
            QTest::qWait(1550); // A prior valid Ov must not authorize stale live state.
        else if (loss == "stale-feedback") {
            controller.withOverrides = false;
            controller.respondToStatus = true;
            QTest::qWait(1550);
            QVERIFY(session.statusFresh());
            QVERIFY(session.overrideFeedback); // Historical value alone is insufficient.
        } else if (loss == "offset")
            session.knownOffset = false;
        else if (loss == "position")
            session.knownPosition = false;
        else if (loss == "output")
            session.knownOutput = false;
        else if (loss == "configuration")
            session.coordinateSystem = false;
        else if (loss == "verification")
            session.connectionVerified = false;
        else if (loss == "feedback") {
            controller.reply("<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0|Ov:bad,100,100>\r\n");
            QTRY_VERIFY(!session.overrideFeedback);
        } else if (loss == "feedback-arity" || loss == "feedback-rapid") {
            controller.reply(loss == "feedback-arity"
                                 ? "<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0|Ov:100,100>\r\n"
                                 : "<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0|Ov:100,bad,100>\r\n");
            QTRY_VERIFY(!session.overrideFeedback);
        } else if (loss == "reset")
            session.awaitingReset = true;
        else if (loss == "homing")
            session.homingPending = true;
        else if (loss == "handshake")
            session.handshaking = true;
        else if (loss == "state") {
            controller.status = "Check";
            controller.report();
            QTRY_COMPARE(session.controllerState, QString("Check"));
        } else if (loss == "bounds")
            session.reportedMachinePosition = QPointF(200.0001, 0);
        else if (loss == "inputs")
            session.activeInputs = true;
        QTest::qWait(10);
        const auto before = controller.realtime.size();
        QVERIFY(!session.status().overridesReady);
        QVERIFY(!session.status().overridesSafetyError.isEmpty());
        const bool accepted = session.overrides(125, 70);
        QTest::qWait(20);
        QCOMPARE(controller.realtime.mid(before), QByteArray());
        QVERIFY(!accepted);
        QCOMPARE(controller.feed, 100);
        QCOMPARE(controller.power, 100);
    }
    void commissionedOverrideDispatchSupportsIdleRunAndHold_data() {
        QTest::addColumn<QString>("controllerState");
        QTest::addColumn<int>("feed");
        QTest::addColumn<int>("power");
        QTest::newRow("idle-low") << QString("Idle") << 10 << 10;
        QTest::newRow("run-high") << QString("Run") << 200 << 100;
        QTest::newRow("hold-adjust") << QString("Hold:0") << 125 << 70;
    }
    void commissionedOverrideDispatchSupportsIdleRunAndHold() {
        QFETCH(QString, controllerState);
        QFETCH(int, feed);
        QFETCH(int, power);
        Controller controller;
        controller.withOverrides = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.status = controllerState;
        controller.report();
        QTRY_COMPARE(session.controllerState, controllerState.section(':', 0, 0));
        QVERIFY(session.status().overridesReady);
        QVERIFY(session.status().overridesSafetyError.isEmpty());
        QVERIFY(session.overrides(feed, power));
        QTRY_COMPARE(controller.feed, feed);
        QTRY_COMPARE(controller.power, power);
        QTRY_COMPARE(session.feedOverride, feed);
        QTRY_COMPARE(session.powerOverride, power);
        const auto before = controller.realtime.size();
        QVERIFY(!session.overrides(9, power));
        QVERIFY(!session.overrides(201, power));
        QVERIFY(!session.overrides(feed, 9));
        QVERIFY(!session.overrides(feed, 101));
        QTest::qWait(20);
        QCOMPARE(controller.realtime.mid(before), QByteArray());
    }
    void adapterInitiatedResumeAbortWithOutputLeaseRequestsExactlyOneReset() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.autoAck = false;
        machine.start();
        QVERIFY(machine.accessoryJobActive);
        auto grbl = static_cast<GrblSession *>(machine.session.get());
        grbl->pause();
        controller.status = "Hold";
        controller.report();
        QTRY_VERIFY(grbl->paused && controller.realtime.contains('!'));
        QVERIFY(!machine.accessory.releaseAfterSafeClearance(true));
        controller.respondToStatus = false;
        controller.respondToReset = false;
        const auto resets = controller.realtime.count(char(0x18));
        const auto cut = controller.commands.size();
        grbl->knownOffset =
            false; // Loss after any facade preflight, while its output lease is active.
        grbl->pause();
        QTest::qWait(180);
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(!machine.accessoryJobActive);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!machine.extractionSafetyError().isEmpty());
        QVERIFY(!controller.realtime.contains('~'));
        for (const auto &line : controller.commands.mid(cut))
            QVERIFY(!line.startsWith("G1 ") && !line.startsWith("M4"));
    }
    void unsolicitedStartupDuringOutputLeaseStillRequiresAHostStop() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.autoAck = false;
        machine.start();
        QVERIFY(machine.accessoryJobActive);
        QTest::qWait(20); // Drain the accepted initial RX window.
        controller.respondToStatus = false;
        controller.respondToReset = false;
        const auto resets = controller.realtime.count(char(0x18));
        machine.accessoryWatchdog.stop(); // Order session feedback before watchdog evaluation.
        controller.reply("Grbl 1.3a ['$' for help]\r\n");
        QTest::qWait(10);
        controller.reply("ok\r\n"); // Fresh handshake M5 ACK triggers changed, not a host reset.
        QTest::qWait(20);
        machine.accessoryWatchdog.start(50);
        QTest::qWait(100);
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(!machine.accessoryJobActive);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!machine.extractionSafetyError().isEmpty());
        QVERIFY(!controller.realtime.contains('~'));
    }
    void rawReportedMachineBoundsAreCheckedBeforeStatusCanContinueStreaming_data() {
        QTest::addColumn<QRectF>("envelope");
        QTest::addColumn<QPointF>("offset");
        QTest::addColumn<QByteArray>("report");
        QTest::addColumn<QPointF>("expectedWork");
        QTest::addColumn<bool>("allowed");
        QTest::addColumn<bool>("streaming");
        const QRectF positive(0, 0, 200, 150), negative(-200, -150, 200, 150);
        QTest::newRow("idle-positive-exact-edge")
            << positive << QPointF() << QByteArray("<Idle|MPos:200,150,0|WCO:0,0,0|FS:0,0>")
            << QPointF(200, 150) << true << false;
        QTest::newRow("idle-positive-small-overstep")
            << positive << QPointF() << QByteArray("<Idle|MPos:200.0001,75,0|WCO:0,0,0|FS:0,0>")
            << QPointF() << false << false;
        QTest::newRow("run-positive-small-overstep")
            << positive << QPointF() << QByteArray("<Run|MPos:200.0001,75,0|WCO:0,0,0|FS:1000,200>")
            << QPointF() << false << true;
        QTest::newRow("hold-positive-small-understep")
            << positive << QPointF() << QByteArray("<Hold|MPos:-0.0001,75,0|WCO:0,0,0|FS:0,0>")
            << QPointF() << false << true;
        QTest::newRow("idle-negative-exact-edge")
            << negative << QPointF(-200, -150)
            << QByteArray("<Idle|MPos:-200,-150,0|WCO:-200,-150,0|FS:0,0>") << QPointF() << true
            << false;
        QTest::newRow("run-negative-small-overstep")
            << negative << QPointF(-200, -150)
            << QByteArray("<Run|MPos:0.0001,-75,0|WCO:-200,-150,0|FS:1000,200>") << QPointF()
            << false << true;
        QTest::newRow("hold-negative-small-understep")
            << negative << QPointF(-200, -150)
            << QByteArray("<Hold|MPos:-200.0001,-75,0|WCO:-200,-150,0|FS:0,0>") << QPointF()
            << false << true;
        QTest::newRow("idle-shifted-work-exact-edge")
            << positive << QPointF(50, 25) << QByteArray("<Idle|WPos:150,125,0|WCO:50,25,0|FS:0,0>")
            << QPointF(150, 125) << true << false;
        QTest::newRow("run-shifted-work-small-overstep")
            << positive << QPointF(50, 25)
            << QByteArray("<Run|WPos:150.0001,50,0|WCO:50,25,0|FS:1000,200>") << QPointF() << false
            << true;
        QTest::newRow("run-machine-counter-beats-rounded-work-counter")
            << positive << QPointF(50, 25)
            << QByteArray("<Run|MPos:200.0001,75,0|WPos:150,50,0|WCO:50,25,0|FS:1000,200>")
            << QPointF() << false << true;
    }
    void rawReportedMachineBoundsAreCheckedBeforeStatusCanContinueStreaming() {
        QFETCH(QRectF, envelope);
        QFETCH(QPointF, offset);
        QFETCH(QByteArray, report);
        QFETCH(QPointF, expectedWork);
        QFETCH(bool, allowed);
        QFETCH(bool, streaming);
        Controller controller;
        controller.machinePosition = envelope.left() < 0 ? QPointF(-100, -75) : QPointF(100, 75);
        controller.workOffset = offset;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        // Homing permission is independent; no positive-envelope assumption is
        // supplied until this fixture's signed physical frame is known.
        session.commissionedHomingContract = true;
        session.home();
        QTRY_VERIFY(session.ready() && session.status().positionReferenced);
        session.commissionedEnvelope = envelope;
        session.commissionedModalContract = true;
        QVERIFY(session.motionSafetyError().isEmpty());
        controller.respondToStatus = false;
        const auto first = controller.commands.size();
        int credits = 0;
        if (streaming) {
            controller.autoAck = false;
            JobPlan plan;
            QPointF previous(10, 10);
            for (int i = 0; i < 300; ++i) {
                const QPointF next = i % 2 ? QPointF(10, 10) : QPointF(20, 20);
                plan.moves.append({previous, next, 1000, 20, 0, false});
                previous = next;
            }
            session.start(plan);
            QVERIFY(session.busy());
            bool poweredTarget = false;
            for (int attempt = 0; attempt < 100 && !poweredTarget; ++attempt) {
                QTest::qWait(3);
                for (int i = first; i < controller.commands.size(); ++i)
                    poweredTarget |= controller.commands[i].startsWith("G1 ");
                if (!poweredTarget && controller.commands.size() - first > credits) {
                    controller.reply("ok\r\n");
                    ++credits;
                }
            }
            QVERIFY(poweredTarget);
        }
        const auto resets = controller.realtime.count(char(0x18));
        QTest::qWait(10); // Drain already accepted bytes before the bad readback.
        const auto faultCut = controller.commands.size();
        QByteArray packet = report + "\r\n";
        if (!allowed) {
            for (qsizetype i = 0; i < controller.commands.size() - first - credits; ++i)
                packet += "ok\r\n";
            // A false in-range return in the same input packet must not revive
            // the discarded connection, reference, or remaining job queue.
            packet += "<Idle|MPos:" + QByteArray::number(controller.machinePosition.x()) + "," +
                      QByteArray::number(controller.machinePosition.y()) +
                      ",0|WCO:" + QByteArray::number(offset.x()) + "," +
                      QByteArray::number(offset.y()) + ",0|FS:0,0>\r\n";
        }
        controller.reply(packet);
        QTest::qWait(40);
        if (allowed) {
            QVERIFY(session.connected());
            QVERIFY(session.status().positionReferenced);
            QCOMPARE(session.position, expectedWork);
            QVERIFY(session.motionSafetyError().isEmpty());
            QCOMPARE(controller.realtime.count(char(0x18)), resets);
        } else {
            QVERIFY2(!session.connected(),
                     "Out-of-envelope status must abort before processing later ACKs/status.");
            QTRY_COMPARE(controller.realtime.count(char(0x18)), resets + 1);
            QVERIFY(!session.status().positionReferenced);
            QVERIFY(!session.commissionedModalContract);
            QVERIFY(!session.commissionedHomingContract);
            for (const auto &command : controller.commands.mid(faultCut))
                QVERIFY(!command.startsWith("G1 ") && !command.startsWith("M4"));
            bool typedFault = false;
            for (const auto &item : session.diagnosticSnapshot()["events"].toList()) {
                const auto event = item.toMap();
                typedFault |=
                    event["event"] == "fault" && event["reason"] == "reportedPhysicalBounds";
            }
            QVERIFY(typedFault);
        }
        QVERIFY(!controller.realtime.contains('~'));
    }
    void homingPhaseExemptionDoesNotApproveAnOutOfEnvelopeReturn() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = false;
        controller.respondToStatus = false;
        const auto homes = controller.commands.count("$H");
        session.home();
        QTest::qWait(10);
        controller.reply("ok\r\n");
        QTRY_COMPARE(controller.commands.count("$H"), homes + 1);
        controller.reply("<Home|MPos:250.0001,20,0|WCO:0,0,0|FS:0,0>\r\n");
        QTest::qWait(20);
        QVERIFY(session.connected()); // Authorized homing is not ordinary bounded job motion.
        QVERIFY(session.homingPending);
        QVERIFY(!session.status().positionReferenced);
        controller.reply("ok\r\n<Idle|MPos:250.0001,20,0|WCO:0,0,0|FS:0,0>\r\n"
                         "<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().positionReferenced);
        QVERIFY(!session.commissionedModalContract);
        QVERIFY(!controller.realtime.contains('~'));
    }
    void unknownPhysicalConventionIsNotGuessedAndLaterGrantCannotHideUnsafeIdlePosition() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        controller.respondToStatus = false;
        controller.reply("<Idle|MPos:-250.0001,-20,0|WCO:0,0,0|FS:0,0>\r\n");
        QTRY_COMPARE(session.position, QPointF(-250.0001, -20));
        QVERIFY(
            session.connected()); // No private signed envelope means no guessed positive limits.
        QVERIFY(!session.motionSafetyError().isEmpty());
        session.commissionedEnvelope = QRectF(0, 0, 200, 150);
        session.commissionedModalContract = true;
        session.positionReferenced =
            true; // Deliberately unsafe synthetic grant, never production API.
        QVERIFY(session.motionSafetyError().contains("Fahrraum"));
        const auto before = controller.commands;
        session.jog(1, 0);
        QTest::qWait(20);
        QCOMPARE(controller.commands, before);
    }
    void focusAcquiresExtractionLeaseUntilReleaseOrFault() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.autoAck = false;
        machine.focus(true);
        QVERIFY(machine.firing());
        QVERIFY(machine.accessoryFocusActive);
        QVERIFY(!machine.accessory.releaseAfterSafeClearance(true));
        QVERIFY(!machine.accessory.request(AccessoryContract::Role::EnclosureExtraction, false));
        machine.focus(false);
        QVERIFY(!machine.firing());
        QVERIFY(!machine.accessoryFocusActive);
        QVERIFY(!machine.extractionSafetyError().isEmpty());
        QVERIFY(!controller.commands.contains("M9"));
    }
    void idleDisconnectAndUnsolicitedResetInvalidateAccessoryEvidence() {
        for (const bool reset : {false, true}) {
            Controller controller;
            Document document;
            document.setBackgroundPlanning(false);
            Machine machine(&document);
            QVERIFY(machine.connectHardware(controller.port));
            QTRY_VERIFY(machine.ready());
            requireReference(machine);
            QVERIFY(machine.extractionSafetyError().isEmpty());
            QVERIFY(!machine.accessoryJobActive);
            if (reset) {
                controller.reply("Grbl 1.3a ['$' for help]\r\n");
                QTRY_VERIFY(
                    !machine.accessory.verified(AccessoryContract::Role::EnclosureExtraction) ||
                    machine.accessory.state(AccessoryContract::Role::EnclosureExtraction)
                            .physical == AccessoryContract::PhysicalState::Unknown);
            } else {
                machine.toggleConnection();
                QVERIFY(!machine.connected());
                QCOMPARE(
                    machine.accessory.state(AccessoryContract::Role::EnclosureExtraction).physical,
                    AccessoryContract::PhysicalState::Unknown);
            }
            QVERIFY(!machine.extractionSafetyError().isEmpty());
            QVERIFY(!controller.commands.contains("M9"));
        }
    }
    void idleAirflowExpiryNotifiesConsumersWithoutIncomingControllerReports() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        QTRY_VERIFY(machine.lastProcessingSafetyErrors.isEmpty());
        QSignalSpy changes(&machine, &Simulator::changed);
        controller.respondToStatus = false;
        QTRY_VERIFY_WITH_TIMEOUT(!machine.extractionSafetyError().isEmpty(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(
            machine.lastProcessingSafetyErrors.contains(machine.extractionSafetyError()), 500);
        QVERIFY(!changes.isEmpty());
        QVERIFY(!machine.accessoryJobActive);
    }
    void facadeRejectsUnknownExtractionDespiteSyntheticMotionContract() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        machine.accessory = AccessoryContract(true); // No real mapping exists in production.
        QVERIFY(machine.motionSafetyError().isEmpty());
        QVERIFY(machine.extractionSafetyError().contains("Ausgangszuordnung"));
        const auto before = controller.commands;
        machine.start();
        machine.focus(true);
        QTest::qWait(60);
        QCOMPARE(controller.commands, before);
        QVERIFY(!machine.firing());
    }
    void lostAirflowStopsAnActiveSyntheticJobWithoutResumeOrAccessoryOff() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        controller.status = "Run";
        machine.start();
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
        const auto resetCount = controller.realtime.count(char(0x18));
        QTRY_VERIFY_WITH_TIMEOUT(controller.realtime.count(char(0x18)) > resetCount, 4500);
        QVERIFY(!controller.realtime.contains('~'));
        QVERIFY(!controller.commands.contains("M9"));
        QVERIFY(!machine.accessoryJobActive);
    }
    void sessionResumeRejectsLostMotionEvidenceBeforeCycleStart_data() {
        QTest::addColumn<QString>("loss");
        for (const auto &loss :
             {"stale", "noGrants", "reference", "outsideEnvelope", "configuration", "offset"})
            QTest::newRow(loss) << QString(loss);
    }
    void sessionResumeRejectsLostMotionEvidenceBeforeCycleStart() {
        QFETCH(QString, loss);
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        controller.autoAck = false;
        QVERIFY(session.commands({"G4 P1", "M5 S0"}));
        session.pause(); // Safe hold is allowed before any resume decision.
        controller.status = "Hold";
        controller.report();
        QTRY_VERIFY(session.paused && controller.realtime.contains('!'));
        controller.respondToStatus = false;
        const auto resets = controller.realtime.count(char(0x18));
        if (loss == "stale")
            QTest::qWait(1700);
        else if (loss == "noGrants") {
            session.commissionedEnvelope.reset();
            session.commissionedModalContract = false;
            session.positionReferenced = false;
        } else if (loss == "reference")
            session.positionReferenced = false;
        else if (loss == "outsideEnvelope") {
            controller.reply("<Hold|MPos:250,20,0|WCO:0,0,0|FS:0,0>\r\n");
            QTRY_COMPARE(session.position, QPointF(250, 20));
        } else if (loss == "configuration") {
            controller.reply("$32=0\r\n");
            QTRY_VERIFY(!session.laserMode);
        } else
            session.knownOffset = false;
        session.pause(); // This would otherwise send cycle-start '~'.
        QTRY_VERIFY_WITH_TIMEOUT(controller.realtime.count(char(0x18)) > resets ||
                                     controller.realtime.contains('~'),
                                 1000);
        QVERIFY2(!controller.realtime.contains('~'),
                 qPrintable("Unsafe cycle-start after loss: " + loss));
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(!session.status().positionReferenced);
    }
    void sessionJobPauseCannotInterfereWithResetOrExplicitHoming_data() {
        QTest::addColumn<bool>("homingPhase");
        QTest::newRow("reset-boundary") << false;
        QTest::newRow("explicit-homing") << true;
    }
    void sessionJobPauseCannotInterfereWithResetOrExplicitHoming() {
        QFETCH(bool, homingPhase);
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        if (homingPhase) {
            session.commissionedHomingContract = true; // Separate synthetic homing evidence only.
            controller.autoAck = false;
            session.home();
            QTRY_VERIFY(controller.commands.contains("M5"));
            controller.reply("ok\r\n");
            QTRY_VERIFY(controller.commands.contains("$H"));
            QVERIFY(session.homingPending);
        } else {
            controller.respondToReset = false;
            session.stop();
            QTRY_VERIFY(controller.realtime.count(char(0x18)) == 2);
            QVERIFY(session.awaitingReset);
        }
        const auto before = controller.realtime;
        session.pause();
        session.pause();
        QTest::qWait(40);
        QCOMPARE(controller.realtime, before); // No feed-hold, cycle-start, or extra reset.
        QVERIFY(!controller.realtime.contains('~'));
        QVERIFY(!session.paused);
    }
    void safeHoldStillWorksWithoutMotionGrantsButCannotResume() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.motionSafetyError().isEmpty());
        controller.autoAck = false;
        QVERIFY(session.commands({"G4 P1", "M5 S0"})); // Private, non-motion synthetic setup.
        const auto holds = controller.realtime.count('!');
        const auto resets = controller.realtime.count(char(0x18));
        session.pause();
        QTRY_COMPARE(controller.realtime.count('!'), holds + 1);
        QCOMPARE(controller.realtime.count(char(0x18)), resets);
        QVERIFY(session.paused);
        session.pause();
        QTRY_VERIFY_WITH_TIMEOUT(controller.realtime.count(char(0x18)) > resets ||
                                     controller.realtime.contains('~'),
                                 1000);
        QVERIFY(!controller.realtime.contains('~'));
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
    }
    void protectiveStopDoesNotResendResetWhileAwaitingStartup() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        controller.respondToReset = false;
        const auto before = controller.realtime.count(char(0x18));
        machine.stop();
        QTest::qWait(180); // Several normal 50ms watchdog evaluations.
        QCOMPARE(controller.realtime.count(char(0x18)), before + 1);
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!controller.realtime.contains('~'));
    }
    void explicitRepeatedHomingMayWaitForAcknowledgementWithoutWatchdogReset() {
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine); // Previously referenced, with private synthetic homing evidence.
        QVERIFY(machine.positionReferenced());
        const auto resets = controller.realtime.count(char(0x18));
        const auto homes = controller.commands.count("$H");
        controller.status = "Home";
        controller.autoAck = false;
        machine.home();
        QTest::qWait(10);
        controller.reply("ok\r\n"); // M5, then the explicit $H may be queued.
        QTest::qWait(230);          // Real homing need not complete within one watchdog tick.
        QVERIFY2(
            controller.realtime.count(char(0x18)) == resets &&
                controller.commands.count("$H") == homes + 1,
            qPrintable(
                QString("Explicit homing was reset: resets=%1 (before=%2), homes=%3 (before=%4)")
                    .arg(controller.realtime.count(char(0x18)))
                    .arg(resets)
                    .arg(controller.commands.count("$H"))
                    .arg(homes)));
        QVERIFY(!machine.positionReferenced());
        controller.status = "Idle";
        controller.autoAck = true;
        controller.reply("ok\r\n"); // Homing ACK; fresh Idle/WCO follows via the existing query.
        QTRY_VERIFY(machine.ready() && machine.positionReferenced());
        QCOMPARE(controller.realtime.count(char(0x18)), resets);
        QVERIFY(!controller.realtime.contains('~'));
    }
    void lostPositionVerificationStopsStreamingBeforeFurtherCredits_data() {
        QTest::addColumn<bool>("silentStatus");
        QTest::newRow("invalid-run-position") << false;
        QTest::newRow("stale-run-status-before-watchdog") << true;
    }
    void lostPositionVerificationStopsStreamingBeforeFurtherCredits() {
        QFETCH(bool, silentStatus);
        Controller controller;
        Document document;
        document.setBackgroundPlanning(false);
        document.newProject();
        document.addShape("rect");
        document.setProperty("x", 10.);
        document.setProperty("y", 10.);
        document.setLayer(0);
        document.setLayerSetting(0, "mode", "Gravur");
        document.setLayerSetting(0, "enabled", true);
        document.setLayerSetting(0, "interval", .5);
        document.setProperty("width", 120.);
        document.setProperty("height", 80.);
        QVERIFY(document.job().moves.size() > 100);
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        // Independent synthetic airflow evidence. Never a real-device grant.
        machine.accessory = AccessoryContract(true);
        QVERIFY(machine.accessory.configure(
            AccessoryContract::Role::EnclosureExtraction,
            {"synthetic-stream-extraction", "PTY regression only", true}));
        const auto request =
            machine.accessory.request(AccessoryContract::Role::EnclosureExtraction, true);
        QVERIFY(request);
        QVERIFY(machine.accessory.acknowledge(*request, true));
        const auto now = machine.accessoryClock.elapsed();
        QVERIFY(machine.accessory.observe(AccessoryContract::Role::EnclosureExtraction,
                                          AccessoryContract::PhysicalState::Flowing,
                                          "synthetic flow", now, now));
        QVERIFY(machine.processingSafetyErrors().isEmpty());
        controller.autoAck = false;
        controller.status = "Run";
        const auto first = controller.commands.size();
        const auto resets = controller.realtime.count(char(0x18));
        machine.start();
        QVERIFY(machine.running());
        QVERIFY(machine.accessoryJobActive);
        int credits = 0;
        bool poweredTargetQueued = false;
        for (int attempt = 0; attempt < 100 && !poweredTargetQueued; ++attempt) {
            QTest::qWait(5);
            for (int i = first; i < controller.commands.size(); ++i)
                poweredTargetQueued |= controller.commands[i].startsWith("G1 ");
            if (!poweredTargetQueued && controller.commands.size() - first > credits) {
                controller.reply("ok\r\n");
                ++credits;
            }
        }
        QVERIFY(poweredTargetQueued);
        QVERIFY(machine.running());
        controller.respondToStatus = false;
        if (silentStatus) {
            // Deliberately order ControllerSession.changed before watchdog
            // evaluation. Safety must not depend on which overdue Qt timer or
            // incoming status callback runs first after a delayed event loop.
            machine.accessoryWatchdog.stop();
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 1900 && machine.sessionWasVerified &&
                   controller.realtime.count(char(0x18)) == resets) {
                if (controller.commands.size() - first > credits) {
                    controller.reply("ok\r\n");
                    ++credits;
                }
                QTest::qWait(50); // ACK streaming remains active without status feedback.
            }
            QVERIFY(!machine.connectionVerified() ||
                    controller.realtime.count(char(0x18)) > resets);
            machine.accessoryWatchdog.start(50);
        } else {
            controller.reply("<Run|MPos:nan,0,0|WCO:0,0,0|FS:5000,800>\r\n");
            QTest::qWait(20);
        }
        // Allow the normal 50ms watchdog to act, with no additional credits
        // that could be confused with commands accepted before the fault.
        QTest::qWait(90);
        const auto afterGuard = controller.commands.size();
        controller.autoAck = true;
        if (controller.realtime.count(char(0x18)) > resets) {
            controller.reply("ok\r\n"); // Permit only the new handshake's pending safety-off ACK.
        } else {
            const auto outstanding = controller.commands.size() - first - credits;
            QVERIFY(outstanding > 0);
            for (qsizetype i = 0; i < outstanding; ++i)
                controller.reply("ok\r\n");
        }
        QTest::qWait(80);
        QStringList unsafeContinuation;
        for (int i = afterGuard; i < controller.commands.size(); ++i)
            if (controller.commands[i].startsWith("G1 ") || controller.commands[i].startsWith("M4"))
                unsafeContinuation.append(controller.commands[i]);
        const bool resetRequested = controller.realtime.count(char(0x18)) > resets;
        QVERIFY2(
            resetRequested && unsafeContinuation.isEmpty(),
            qPrintable(
                QString("After lost position verification: reset=%1; subsequent G1/M4=%2; first=%3")
                    .arg(resetRequested)
                    .arg(unsafeContinuation.size())
                    .arg(unsafeContinuation.isEmpty() ? QString("none")
                                                      : unsafeContinuation.first())));
        QVERIFY(!machine.positionReferenced());
        QVERIFY(!controller.realtime.contains('~'));
    }
    void diagnosticDoesNotMislabelNormalizedHandshakeRejectionAsAnAcknowledgement() {
        Controller controller;
        controller.resetState = "Alarm";
        controller.rejectHandshakeSafetyOff = true;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_COMPARE(session.state, QString("Alarm"));
        const auto snapshot = session.diagnosticSnapshot();
        const auto events = snapshot["events"].toList();
        quint64 dispatch = 0;
        bool rejection = false;
        for (const auto &item : events) {
            const auto event = item.toMap();
            if (event["event"] == "commandQueued" && event["command"] == "safetyOffRequested") {
                QCOMPARE(event["acknowledgement"].toString(), QString("rejectedByController"));
                dispatch = event["sequence"].toULongLong();
            }
        }
        QVERIFY(dispatch > 0);
        for (const auto &item : events) {
            const auto event = item.toMap();
            if (event["dispatchSequence"].toULongLong() != dispatch)
                continue;
            QVERIFY(event["event"] != "commandAcknowledged");
            if (event["event"] == "commandRejected") {
                QCOMPARE(event["code"].toInt(), 9);
                rejection = true;
            }
        }
        QVERIFY(rejection);
        QCOMPARE(snapshot["counters"].toMap()["commandRejectionsReceived"].toInt(), 1);
    }
    void diagnosticFaultPreservesLastNumericReadbackWithoutClaimingCurrentFreshness() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        controller.respondToStatus = false;
        controller.reply("<Idle|MPos:12,34,0|WCO:5,6,0|FS:0,20>\r\n");
        QTRY_VERIFY(!session.connected());
        const auto snapshot = session.diagnosticSnapshot();
        const auto machine = snapshot["positions"].toMap()["machineXY"].toMap();
        QVERIFY(machine["known"].toBool());
        QVERIFY(!machine["fresh"].toBool());
        QCOMPARE(machine["xMm"].toDouble(), 12.);
        QCOMPARE(machine["yMm"].toDouble(), 34.);
        const auto output = snapshot["controllerOutputReadback"].toMap();
        QVERIFY(output["known"].toBool());
        QVERIFY(!output["fresh"].toBool());
        QCOMPARE(output["value"].toDouble(), 20.);
        QVERIFY(!snapshot["state"].toMap()["statusFresh"].toBool());
        QVERIFY(!snapshot["state"].toMap()["positionKnown"].toBool());
        bool cause = false;
        for (const auto &item : snapshot["events"].toList()) {
            const auto event = item.toMap();
            cause |= event["event"] == "fault" && event["reason"] == "idleOutputReported";
        }
        QVERIFY(cause);
    }
    void diagnosticRingIsBoundedAndNeverManufacturesInvalidNumericValues() {
        using D = ControllerDiagnostics;
        D audit;
        D::LiveState live;
        for (int i = 0; i < 10000; ++i) {
            audit.request(D::Action::Start);
            audit.deny(D::Action::Start, D::Denial::MotionContract);
        }
        audit.setting(3, 5.);
        audit.setting(24, std::numeric_limits<double>::quiet_NaN());
        audit.setting(999, 123456.);
        audit.coordinate(D::Coordinate::Machine,
                         QPointF(std::numeric_limits<double>::infinity(), 0));
        auto snapshot = audit.snapshot(live);
        const auto encoded =
            QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact);
        QVERIFY(encoded.size() <= D::maximumSnapshotBytes);
        const auto events = snapshot["events"].toList();
        QCOMPARE(events.size(), D::maximumEvents);
        QVERIFY(snapshot["droppedEvents"].toULongLong() >= 19936);
        quint64 previousSequence = 0;
        qint64 previousTime = -1;
        for (const auto &item : events) {
            const auto event = item.toMap();
            QVERIFY(event["sequence"].toULongLong() > previousSequence);
            QVERIFY(event["elapsedMs"].toLongLong() >= previousTime);
            previousSequence = event["sequence"].toULongLong();
            previousTime = event["elapsedMs"].toLongLong();
        }
        const auto readback = snapshot["readback"].toMap();
        QCOMPARE(readback["$3"].toMap()["value"].toDouble(), 5.);
        QVERIFY(!readback["$24"].toMap()["known"].toBool());
        QVERIFY(!readback["$24"].toMap().contains("value"));
        QVERIFY(!readback.contains("$999"));
        const auto machine = snapshot["positions"].toMap()["machineXY"].toMap();
        QVERIFY(!machine["known"].toBool());
        QVERIFY(!machine.contains("xMm"));

        D shape;
        for (auto key : D::settingKeys)
            shape.setting(key, 1e9);
        shape.coordinate(D::Coordinate::Work, QPointF(1e9, -1e9));
        shape.beginStream(true, live);
        for (int i = 0; i < 100; ++i) {
            shape.queued(D::Command::FirstApproachRequested, QPointF(i, i));
            shape.queued(D::Command::FirstPoweredTargetRequested, QPointF(i, i), 1e9);
            shape.queued(D::Command::SafetyOff, QPointF(i, i)); // Must not smuggle coordinates.
        }
        auto bounded = shape.snapshot(live);
        int coordinateEvents = 0;
        for (const auto &item : bounded["events"].toList())
            if (item.toMap().contains("xMm"))
                ++coordinateEvents;
        QCOMPARE(coordinateEvents, 2);
        QVERIFY(QJsonDocument(QJsonObject::fromVariantMap(bounded))
                    .toJson(QJsonDocument::Compact)
                    .size() <= D::maximumSnapshotBytes);

        // If an instrumentation caller ever exceeds the bounded pending window,
        // old ACKs must not be misattributed to retained newer dispatches.
        D pending;
        for (int i = 0; i < 100; ++i)
            pending.queued(D::Command::SafetyOff);
        for (int i = 0; i < 36; ++i)
            pending.acknowledged();
        auto retained = pending.snapshot(live)["events"].toList();
        QCOMPARE(retained.last().toMap()["acknowledgement"].toString(), QString("pending"));
        for (int i = 0; i < 64; ++i)
            pending.acknowledged();
        const auto counts = pending.snapshot(live)["counters"].toMap();
        QCOMPARE(counts["acknowledgementsReceived"].toInt(), 100);
        QCOMPARE(counts["pendingTrackingOverflows"].toInt(), 36);
        QCOMPARE(counts["unmatchedAcknowledgements"].toInt(), 0);
    }
    void diagnosticSnapshotRedactsEndpointPayloadsAndInvalidReadback() {
        Controller controller;
        GrblSession session;
        QCOMPARE(session.ControllerSession::diagnosticSnapshot(),
                 QVariantMap({{"supported", false}}));
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        controller.respondToStatus = false;
        const QByteArray secret("PRIVATE_ENDPOINT_CONTROLLER_PROJECT_TOKEN_730198");
        controller.reply("[VER:1.3a." + secret + ":]\r\n[MSG:" + secret + "]\r\n" + secret +
                         "\r\n");
        controller.reply("$3=5\r\n$5=1\r\n$20=1\r\n$21=1\r\n$23=3\r\n$24=100\r\n$25=500\r\n$26="
                         "250\r\n$27=2\r\n$999=123\r\n$24=" +
                         secret + "\r\n");
        controller.reply("<" + secret + "|MPos:nan,0,0|WCO:nan,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(!session.ready());
        const auto snapshot = session.diagnosticSnapshot();
        const auto bytes =
            QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact);
        QVERIFY(!bytes.contains(secret));
        QVERIFY(!bytes.contains(controller.port.toUtf8()));
        QVERIFY(!bytes.contains("Grbl 1.3a"));
        QVERIFY(!bytes.contains("[MSG:"));
        QVERIFY(bytes.size() <= ControllerDiagnostics::maximumSnapshotBytes);
        const auto readback = snapshot["readback"].toMap();
        QCOMPARE(readback["$23"].toMap()["value"].toInt(), 3);
        QCOMPARE(readback["$27"].toMap()["value"].toDouble(), 2.);
        QVERIFY(!readback["$24"].toMap()["known"].toBool());
        QVERIFY(!readback["$24"].toMap().contains("value"));
        QVERIFY(!readback.contains("$999"));
        QCOMPARE(snapshot["state"].toMap()["controllerState"].toString(), QString("unknown"));
        const auto modal = snapshot["modalReadback"].toMap();
        QCOMPARE(modal["distanceCode"].toMap()["value"].toInt(), 90);
        const auto positions = snapshot["positions"].toMap();
        QVERIFY(!positions["machineXY"].toMap()["known"].toBool());
        QVERIFY(!positions["workXY"].toMap()["known"].toBool());
        QVERIFY(!positions["wcoXY"].toMap()["known"].toBool());
        controller.reply("$13=1\r\n<Idle|MPos:1,2,0|WCO:0,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(!session.reportMm);
        const auto inchSnapshot = session.diagnosticSnapshot();
        QVERIFY(!inchSnapshot["positions"].toMap()["workXY"].toMap()["known"].toBool());
        QVERIFY(!inchSnapshot["positions"].toMap()["workXY"].toMap().contains("xMm"));
        QCOMPARE(inchSnapshot["readback"].toMap()["$13"].toMap()["value"].toInt(), 1);
    }
    void diagnosticsDistinguishResetRequestsHomingAndUnacknowledgedCommands() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        session.home(); // A denied request is not a dispatched homing command.
        auto snapshot = session.diagnosticSnapshot();
        QCOMPARE(snapshot["counters"].toMap()["resetRequests"].toInt(), 1);
        QCOMPARE(snapshot["counters"].toMap()["startupBannersObserved"].toInt(), 1);
        for (const auto &item : snapshot["events"].toList())
            QVERIFY(item.toMap()["command"].toString() != "homingRequested");
        requireReference(session);
        controller.autoAck = false;
        QVERIFY(session.commands({"M5", "G21"}));
        QTest::qWait(30);
        auto events = session.diagnosticSnapshot()["events"].toList();
        QCOMPARE(events.last().toMap()["acknowledgement"].toString(), QString("pending"));
        controller.autoAck = true;
        session.stop();
        QTRY_VERIFY(session.ready());
        snapshot = session.diagnosticSnapshot();
        QCOMPARE(snapshot["counters"].toMap()["resetRequests"].toInt(), 2);
        QVERIFY(!snapshot["state"].toMap()["homingProtocolReferenced"].toBool());
        bool boundary = false, stop = false;
        int homingCommands = 0;
        for (const auto &item : snapshot["events"].toList()) {
            const auto event = item.toMap();
            if (event["event"] == "commandQueued" && event["command"] == "homingRequested")
                ++homingCommands;
            boundary |= event["acknowledgement"] == "notAcknowledgedBeforeBoundary";
            stop |= event["event"] == "resetRequested" && event["reason"] == "stop";
        }
        QCOMPARE(homingCommands, 1);
        QVERIFY(boundary);
        QVERIFY(stop);
    }
    void enabledHomingSettingAndIdleOrAlarmDoNotAuthorizeHoming_data() {
        QTest::addColumn<QString>("controllerState");
        QTest::newRow("Idle") << QString("Idle");
        QTest::newRow("Alarm") << QString("Alarm");
    }
    void enabledHomingSettingAndIdleOrAlarmDoNotAuthorizeHoming() {
        QFETCH(QString, controllerState);
        Controller controller;
        controller.resetState = controllerState;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(controller.commands.contains("$G"));
        QTRY_COMPARE(session.state,
                     controllerState == "Idle" ? QString("Bereit") : controllerState);
        QVERIFY(session.homing); // Controller explicitly reports $22=1.
        QVERIFY(!session.homingSafetyError().isEmpty());
        const auto before = controller.commands;
        session.home();
        QTest::qWait(40);
        QCOMPARE(controller.commands, before);
        QVERIFY(!controller.commands.contains("$H"));
        QVERIFY(!session.status().positionReferenced);
    }
    void homingEvidenceExpiresOnEveryResetDisconnectAndFault_data() {
        QTest::addColumn<QString>("event");
        for (const auto &event : {"stop", "close", "alarm", "fault", "restart"})
            QTest::newRow(event) << QString(event);
    }
    void homingEvidenceExpiresOnEveryResetDisconnectAndFault() {
        QFETCH(QString, event);
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        session.commissionedHomingContract = true;
        QVERIFY(session.homingSafetyError().isEmpty());
        if (event == "stop") {
            session.stop();
            QTRY_VERIFY(session.ready());
        } else if (event == "close") {
            session.close();
            QTRY_VERIFY(controller.commands.count("M5 S0") >= 2); // Drain best-effort close output.
        } else if (event == "alarm") {
            controller.status = "Alarm";
            controller.report();
            QTRY_COMPARE(session.state, QString("Alarm"));
        } else if (event == "fault") {
            controller.reply("error:20\r\n");
            QTRY_VERIFY(!session.connected());
        } else {
            controller.reply("Grbl 1.3a ['$' for help]\r\n");
            QTRY_VERIFY(!session.commissionedHomingContract);
            QTRY_VERIFY(session.ready());
        }
        QVERIFY(!session.commissionedHomingContract);
        QVERIFY(!session.homingSafetyError().isEmpty());
        const auto before = controller.commands;
        session.home();
        QTest::qWait(30);
        QCOMPARE(controller.commands, before);
        QVERIFY(!controller.commands.contains("$H"));
    }
    void homingAloneNeverCommissionsARealMachineCoordinateContract() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        s.commissionedHomingContract = true; // Only homing mechanics are synthetic evidence here.
        s.home();
        QTRY_VERIFY(s.status().positionReferenced && s.ready());
        QVERIFY(!s.motionSafetyError().isEmpty());
        s.commissionedEnvelope = QRectF(0, 0, 200, 150);
        QVERIFY(!s.motionSafetyError().isEmpty()); // Geometry alone proves no modal mapping.
        s.commissionedEnvelope.reset();
        s.commissionedModalContract = true;
        QVERIFY(!s.motionSafetyError().isEmpty()); // Modal mapping alone proves no travel limits.
        s.commissionedModalContract = false;
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto before = c.commands;
        s.start(document.job());
        s.jog(1, 0);
        s.zero();
        s.frame({{"valid", true}, {"x", 0}, {"y", 0}, {"width", 20}, {"height", 20}});
        s.focus(true);
        QTest::qWait(50);
        QCOMPARE(c.commands, before);
        QVERIFY(!s.firing);
        QVERIFY(s.ready()); // Connection diagnostics remain available.
    }
    void machineFacadeExposesCommissioningBlockAndSimulatorRemainsUsable() {
        Controller c;
        Document document;
        Machine machine(&document);
        QVERIFY(machine.motionSafetyError().isEmpty());
        QVERIFY(machine.homingSafetyError().isEmpty());
        QVERIFY(machine.connectHardware(c.port));
        QTRY_VERIFY(machine.ready());
        QVERIFY(!machine.motionSafetyError().isEmpty());
        QVERIFY(!machine.homingSafetyError().isEmpty());
        machine.home();
        QTest::qWait(30);
        QVERIFY(!c.commands.contains("$H"));
        static_cast<GrblSession *>(machine.session.get())->commissionedHomingContract = true;
        machine.home();
        QTRY_VERIFY(machine.positionReferenced());
        QVERIFY(!machine.motionSafetyError().isEmpty());
        machine.useSimulator();
        QVERIFY(machine.motionSafetyError().isEmpty());
        QVERIFY(machine.homingSafetyError().isEmpty());
        QVERIFY(machine.ready());
    }
    void staleIdlePositionCannotAuthorizeMotionOrEmission() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.respondToStatus = false;
        QTest::qWait(1700);
        QVERIFY(!s.ready());
        QVERIFY(!s.verified());
        QVERIFY(!s.motionSafetyError().isEmpty());
        const auto diagnosticState = s.diagnosticSnapshot()["state"].toMap();
        QVERIFY(!diagnosticState["statusFresh"].toBool());
        QVERIFY(diagnosticState["validStatusAgeMs"].toLongLong() >= 1500);
        QVERIFY(
            s.diagnosticSnapshot()["positions"].toMap()["workXY"].toMap()["ageMs"].toLongLong() >=
            1500);
        const auto before = c.commands;
        s.jog(1, 0);
        s.focus(true);
        QTest::qWait(40);
        QCOMPARE(c.commands, before);
        c.report();
        QTRY_VERIFY(s.ready());
    }
    void signedPhysicalEnvelopeUsesMachineCoordinatesRatherThanModelOrigin() {
        Controller c;
        c.machinePosition = {-100, -75};
        c.workOffset = {-200, -150};
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        s.commissionedEnvelope = QRectF(-200, -150, 200, 150);
        JobPlan plan;
        plan.moves = {{{0, 0}, {200, 0}, 1000, 20, 0, false},
                      {{200, 0}, {200, 150}, 1000, 20, 0, false}};
        QVERIFY(plan.valid());
        s.start(plan);
        QTRY_VERIFY(c.commands.contains("G0 X0.000 Y0.000"));
        QTRY_VERIFY(c.commands.contains("G1 X200.000 Y150.000"));
        QTRY_VERIFY(s.ready());
    }
    void firstLaserOffApproachMustFitPhysicalEnvelope() {
        Controller c;
        c.machinePosition = {5, 5};
        c.workOffset = {-10, 0};
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        JobPlan plan;
        plan.moves = {{{0, 0}, {20, 0}, 1000, 20, 0, false}};
        QVERIFY(plan.encodedProgram().valid());
        const auto before = c.commands;
        s.start(plan);
        QTest::qWait(40);
        QCOMPARE(c.commands, before);
    }
    void contradictoryMachineAndWorkCoordinatesAbortReference() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.respondToStatus = false;
        c.reply("<Idle|MPos:100,75,0|WPos:0,0,0|WCO:0,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.status().positionReferenced);
        const auto positions = s.diagnosticSnapshot()["positions"].toMap();
        QVERIFY(!positions["machineXY"].toMap()["known"].toBool());
        QVERIFY(!positions["workXY"].toMap()["known"].toBool());
        QVERIFY(!positions["wcoXY"].toMap()["known"].toBool());
    }
    void unexpectedOffsetChangeInvalidatesReferenceAndBufferedQueue() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.workOffset = {1, 0};
        c.report();
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.status().positionReferenced);
    }
    void bufferedCommandErrorNeverPumpsRemainingJobOrResumes() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.autoAck = false;
        JobPlan plan;
        for (int i = 0; i < 100; ++i)
            plan.moves.append({{double(i), 0}, {double(i + 1), 0}, 1000, 20, 0, false});
        s.start(plan);
        QTRY_VERIFY(s.busy());
        QTest::qWait(40);
        const auto before = c.commands.size();
        const auto resets = c.realtime.count(char(0x18));
        c.reply("error:20\r\nok\r\nok\r\n");
        QTRY_VERIFY(!s.connected());
        QTRY_COMPARE(c.realtime.count(char(0x18)), resets + 1);
        QCOMPARE(c.commands.size(), before);
        QVERIFY(!c.commands.contains("G1 X100.000 Y0.000"));
        QVERIFY(!c.realtime.contains('~'));
        QVERIFY(!s.status().positionReferenced);
    }
    void reportedZeroAfterConnectIsNotAReferencedStartPosition() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        QCOMPARE(session.position, QPointF(0, 0));
        QVERIFY(!session.status().positionReferenced);
        QVERIFY(!controller.commands.contains("$H"));
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto before = controller.commands;
        session.start(document.job());
        session.frame({{"valid", true}, {"x", 0}, {"y", 0}, {"width", 200}, {"height", 150}});
        session.jog(1, 0);
        session.zero();
        QTest::qWait(40);
        QCOMPARE(controller.commands, before); // No G0/G1/G10/M8/M4, even at reported 0/0.
        requireReference(session);
        QVERIFY(controller.commands.contains("$H"));
        session.start(document.job());
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
        session.stop();
        QVERIFY(!session.status().positionReferenced);
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.status().positionReferenced);
        const auto afterReset = controller.commands;
        session.start(document.job());
        QTest::qWait(40);
        QCOMPARE(controller.commands, afterReset);
        session.close();
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        QVERIFY(!session.status().positionReferenced);
    }
    void idleWithoutHomingAcknowledgementDoesNotEstablishReference() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        controller.autoAck = false;
        session.commissionedHomingContract = true;
        session.home();
        QTRY_VERIFY(controller.commands.contains("M5"));
        controller.report();
        QTest::qWait(30);
        QVERIFY(!session.status().positionReferenced);
        controller.reply("ok\r\n");
        QTRY_VERIFY(controller.commands.contains("$H"));
        controller.report();
        QTest::qWait(30);
        QVERIFY(!session.status().positionReferenced);
        controller.reply("error:9\r\n");
        QTRY_VERIFY(!session.connected());
        QVERIFY(!session.status().positionReferenced);
    }
    void everyConnectionResetsHeldControllerBeforeAnyNormalCommand() {
        Controller controller;
        controller.status = "Hold";
        controller.holdBlocksCommands = true;
        Document document;
        document.newProject();
        document.addShape("rect");
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QVERIFY(!machine.connectionVerified());
        QTRY_VERIFY(machine.ready());
        QVERIFY(machine.connectionVerified());
        QCOMPARE(controller.realtime.count(char(0x18)), 1);
        QVERIFY(!controller.realtime.contains('~'));
        QCOMPARE(controller.commands, QStringList({"M5 S0", "$I", "$$", "$G"}));
        requireReference(machine);
        controller.status = "Run";
        machine.start();
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
        QVERIFY(machine.connectionVerified()); // Busy is distinct from verified connectivity.
        machine.pause();
        QVERIFY(machine.connectionVerified());
        machine.stop();
        QVERIFY(!machine.connectionVerified());
        controller.resetState = "Idle";
        QTRY_VERIFY(machine.ready());
        QVERIFY(machine.connectionVerified());
        const auto resets = controller.realtime.count(char(0x18));
        machine.useSimulator();
        QVERIFY(!machine.connectionVerified());
        QVERIFY(machine.connectHardware(controller.port));
        QVERIFY(!machine.connectionVerified());
        QTRY_VERIFY(machine.ready());
        QCOMPARE(controller.realtime.count(char(0x18)), resets + 1);
        QVERIFY(machine.connectionVerified());
    }
    void oldRepliesCannotVerifyAConnectionBeforeItsResetBanner() {
        Controller controller;
        controller.respondToReset = false;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_COMPARE(controller.realtime.count(char(0x18)), 1);
        controller.reply("ok\r\n[VER:1.3a.old:]\r\n$30=1000\r\n$31=0\r\n$32=1\r\n$13=0\r\n$130="
                         "200\r\n$131=150\r\n[GC:G54]\r\n");
        controller.report();
        QTest::qWait(30);
        QVERIFY(controller.commands.isEmpty());
        QVERIFY(!session.ready());
        QVERIFY(!session.verified());
        QVERIFY(!session.configurationValid());
        controller.reply("Grbl 1.3a ['$' for help]\r\n");
        QTRY_VERIFY(session.ready());
        QVERIFY(session.verified());
        session.close();
        QVERIFY(!session.verified());
    }
    void resetNeverUnlocksOrResumesBlockedController_data() {
        QTest::addColumn<QString>("status");
        for (const auto &s : {"Alarm", "Door", "Hold"})
            QTest::newRow(s) << QString(s);
    }
    void resetNeverUnlocksOrResumesBlockedController() {
        QFETCH(QString, status);
        Controller controller;
        controller.resetState = status;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(controller.commands.contains("$G"));
        controller.report();
        QTRY_COMPARE(session.state, status);
        QVERIFY(!session.ready());
        QVERIFY(!session.verified());
        QVERIFY(!controller.realtime.contains('~'));
        QVERIFY(!controller.commands.contains("$X"));
        QVERIFY(!controller.commands.contains("$H"));
    }
    void destroyingSessionClosesItsController_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"idle", "job", "paused", "focus", "unplugged", "closed"})
            QTest::newRow(mode) << QString(mode);
    }
    void destroyingSessionClosesItsController() {
        QFETCH(QString, mode);
        Controller controller;
        QString error;
        auto session = ControllerSessions::create(DeviceProfile::m3Reference(), error);
        QVERIFY2(session, qPrintable(error));
        QVERIFY(session->open(controller.port));
        QTRY_VERIFY(session->status().ready);
        requireReference(*session);
        if (mode == "job" || mode == "paused") {
            Document document;
            document.newProject();
            document.addShape("rect");
            controller.status = "Run";
            session->start(document.job());
            QTRY_VERIFY(controller.commands.contains("M4 S800"));
            QVERIFY(session->status().executing);
            if (mode == "paused") {
                session->pause();
                QVERIFY(session->status().paused);
            }
        } else if (mode == "focus") {
            session->focus(true);
            QVERIFY(session->status().firing);
        } else if (mode == "unplugged") {
            controller.disconnectPort();
            QTRY_VERIFY(!session->status().connected);
        } else if (mode == "closed") {
            session->close();
            QTRY_VERIFY(controller.commands.count("M5 S0") >= 2);
        }
        const auto resetCount = controller.realtime.count(char(0x18));
        const auto offCount = controller.commands.count("M5 S0");
        QSignalSpy changes(session.get(), &ControllerSession::changed);
        const auto beforeDestruction = changes.count();
        session.reset();
        if (mode == "job" || mode == "paused" || mode == "focus")
            QTRY_COMPARE_WITH_TIMEOUT(controller.realtime.count(char(0x18)), resetCount + 1, 1000);
        else if (mode == "idle")
            QTRY_COMPARE_WITH_TIMEOUT(controller.commands.count("M5 S0"), offCount + 1, 1000);
        else if (mode == "closed") {
            QTest::qWait(20);
            QCOMPARE(controller.commands.count("M5 S0"), offCount);
            QCOMPARE(controller.realtime.count(char(0x18)), resetCount);
        }
        // Destruction must not notify consumers through a partially destroyed session.
        QCOMPARE(changes.count(), beforeDestruction);
    }
    void destroyingMachineStopsItsController() {
        Controller controller;
        Document document;
        document.newProject();
        document.addShape("rect");
        auto machine = std::make_unique<Machine>(&document);
        QVERIFY(machine->connectHardware(controller.port));
        QTRY_VERIFY(machine->ready());
        requireReference(*machine);
        controller.status = "Run";
        machine->start();
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
        QVERIFY(machine->running());
        QSignalSpy changes(machine.get(), &Simulator::changed);
        const auto resetCount = controller.realtime.count(char(0x18));
        machine.reset();
        QTRY_COMPARE_WITH_TIMEOUT(controller.realtime.count(char(0x18)), resetCount + 1, 1000);
        QCOMPARE(changes.count(), 0);
    }
    void sessionRegistryRejectsUnknownAndMalformedProfiles() {
        QString error;
        const auto reference = DeviceProfile::m3Reference();
        auto session = ControllerSessions::create(reference, error);
        QVERIFY2(session, qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(session->deviceProfile().outputFingerprint(), reference.outputFingerprint());
        QVERIFY(!session->status().connected);
        QVERIFY(!session->status().ready);
        for (int variant = 0; variant < 3; ++variant) {
            auto json = reference.toJson();
            auto driver = json["driver"].toObject();
            if (variant == 0)
                driver["id"] = "unregistered-controller";
            else if (variant == 1)
                driver["transport"] = "network";
            else {
                auto settings = driver["settings"].toObject();
                settings["baudRate"] = "not a number";
                driver["settings"] = settings;
            }
            json["driver"] = driver;
            const auto profile = DeviceProfile::parse(QJsonDocument(json).toJson(), error);
            QVERIFY2(profile, qPrintable(error));
            QVERIFY(!ControllerSessions::create(*profile, error));
            QVERIFY(!error.isEmpty());
            // Rejected profiles never even enumerate real serial endpoints.
            QVERIFY(ControllerSessions::availableEndpoints(*profile).isEmpty());
        }
    }
    void genericSessionKeepsReadinessPauseStopAndNoResume() {
        Controller controller;
        QString error;
        auto session = ControllerSessions::create(DeviceProfile::m3Reference(), error);
        QVERIFY2(session, qPrintable(error));
        QSignalSpy changes(session.get(), &ControllerSession::changed);
        QVERIFY(session->open(controller.port));
        QTRY_VERIFY(session->status().ready);
        requireReference(*session);
        controller.machinePosition = {12, 18};
        controller.report();
        QTRY_COMPARE(session->status().position, QPointF(12, 18));
        const auto snapshot = session->status();
        controller.machinePosition = {20, 30};
        controller.report();
        QTRY_COMPARE(session->status().position, QPointF(20, 30));
        QCOMPARE(snapshot.position, QPointF(12, 18));
        Document document;
        document.newProject();
        document.addShape("rect");
        controller.status = "Run";
        session->start(document.job());
        QTRY_VERIFY(session->status().executing);
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
        session->pause();
        QVERIFY(session->status().paused);
        QVERIFY(!session->status().ready);
        session->pause();
        QVERIFY(!session->status().paused);
        session->stop();
        controller.status = "Idle";
        QTRY_VERIFY(session->status().ready);
        const auto afterStop = controller.commands.count("M4 S800");
        session->close();
        QVERIFY(!session->status().connected);
        QVERIFY(!session->status().ready);
        QVERIFY(snapshot.connected);
        QVERIFY(session->open(controller.port));
        QTRY_VERIFY(session->status().ready);
        QCOMPARE(controller.commands.count("M4 S800"), afterStop);
        QVERIFY(!session->status().executing);
        QVERIFY(!session->logEntries().isEmpty());
        QVERIFY(!changes.isEmpty());
        QPointer<ControllerSession> lifetime = session.get();
        session.reset();
        QVERIFY(lifetime.isNull());
    }
    void changedMovesCannotSendAnOlderCachedProgram() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        Document document;
        document.newProject();
        document.addShape("rect");
        auto plan = document.job();
        plan.cacheCommands();
        QVERIFY(plan.cachedCommands().contains("M4 S800"));
        for (qsizetype index = 0; index < plan.moves.size(); ++index) {
            auto move = plan.moves[index];
            if (!move.rapid) {
                move.power = 20;
                plan.moves.replace(index, move);
            }
        }
        QVERIFY(plan.cachedCommands().isEmpty());
        session.start(plan);
        QTRY_VERIFY(controller.commands.contains("M4 S200"));
        QVERIFY(!controller.commands.contains("M4 S800"));
    }
    void connectedMachinePreventsProjectProfileReplacement() {
        Controller controller;
        Document document;
        document.newProject();
        Machine machine(&document);
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
        auto descriptor = DeviceProfile::m3Reference().toJson();
        auto area = descriptor["workspace"].toObject();
        area["width"] = 600;
        area["height"] = 400;
        descriptor["workspace"] = area;
        QString error;
        const auto other = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(other);
        const auto bytes = document.projectBytes();
        const auto revision = document.historyRevision();
        QVERIFY(!document.setDeviceProfile(*other));
        QCOMPARE(document.projectBytes(), bytes);
        QCOMPARE(document.historyRevision(), revision);
        const auto beforeClose = controller.commands.size();
        machine.toggleConnection();
        QTRY_COMPARE(controller.commands.size(), beforeClose + 1);
        QCOMPARE(controller.commands.last(), QString("M5 S0"));
        QVERIFY(document.setDeviceProfile(*other));
        QVERIFY(machine.simulation());
        const auto before = controller.commands.size();
        QVERIFY(!machine.connectHardware(controller.port));
        QTest::qWait(20);
        QCOMPARE(controller.commands.size(), before);
        QVERIFY(document.setDeviceProfile(DeviceProfile::m3Reference()));
        QVERIFY(machine.connectHardware(controller.port));
        QTRY_VERIFY(machine.ready());
    }
    void foreignJobProfilesNeverSendTheirCachedCommands() {
        Controller controller;
        GrblSession session;
        QVERIFY(session.open(controller.port));
        QTRY_VERIFY(session.ready());
        requireReference(session);
        Document document;
        document.newProject();
        document.addShape("rect");
        const auto base = document.deviceProfile().toJson();
        QList<QJsonObject> profiles;
        for (const QString field : {"powerScale", "baudRate", "originCode"}) {
            auto changed = base;
            auto driver = changed["driver"].toObject();
            auto settings = driver["settings"].toObject();
            settings[field] = field == "powerScale" ? 255 : field == "baudRate" ? 250000 : 1;
            driver["settings"] = settings;
            changed["driver"] = driver;
            profiles.append(changed);
        }
        auto changed = base;
        auto workspace = changed["workspace"].toObject();
        workspace["width"] = 600;
        changed["workspace"] = workspace;
        profiles.append(changed);
        changed = base;
        auto laser = changed["laser"].toObject();
        laser["nominalPowerW"] = 10;
        changed["laser"] = laser;
        profiles.append(changed);
        changed = base;
        auto capabilities = changed["capabilities"].toObject();
        capabilities["rotary"] = true;
        changed["capabilities"] = capabilities;
        profiles.append(changed);
        for (const auto &descriptor : profiles) {
            QString error;
            const auto profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
            QVERIFY2(profile, qPrintable(error));
            auto input = document.jobInput();
            input.deviceProfile = *profile;
            auto plan = JobPlanner::build(input);
            QVERIFY2(plan.valid(), qPrintable(plan.errors.join(";")));
            plan.cacheCommands();
            QCOMPARE(!plan.cachedCommands().isEmpty(),
                     ControllerAdapters::outputError(*profile).isEmpty());
            const auto before = controller.commands.size();
            session.start(plan);
            QTest::qWait(10);
            QCOMPARE(controller.commands.size(), before);
            QVERIFY(session.ready());
            QVERIFY(session.log.first().contains("Geräteprofil des Auftrags"));
        }
        changed = base;
        changed["name"] = "Anderer Anzeigename";
        changed["provenance"] = QJsonObject{{"note", "Nur Metadaten"}};
        QString error;
        const auto renamed = DeviceProfile::parse(QJsonDocument(changed).toJson(), error);
        QVERIFY(renamed);
        auto input = document.jobInput();
        input.deviceProfile = *renamed;
        session.start(JobPlanner::build(input));
        QTRY_VERIFY(controller.commands.contains("M4 S800"));
    }
    void unwiredProfileCannotOpenEvenAVirtualPort() {
        Controller controller;
        auto descriptor = DeviceProfile::m3Reference().toJson();
        auto driver = descriptor["driver"].toObject();
        auto settings = driver["settings"].toObject();
        settings["powerScale"] = 255;
        driver["settings"] = settings;
        descriptor["driver"] = driver;
        QString error;
        const auto profile = DeviceProfile::parse(QJsonDocument(descriptor).toJson(), error);
        QVERIFY(profile);
        GrblSession session(nullptr, *profile);
        QVERIFY(!session.open(controller.port));
        QVERIFY(!session.connected());
        QTest::qWait(20);
        QVERIFY(controller.commands.isEmpty());
        QVERIFY(!controller.banner);
    }
    void jobBoundsIncludeWorkOffset_data() {
        QTest::addColumn<QPointF>("offset");
        QTest::addColumn<QPointF>("current");
        QTest::addColumn<QRectF>("rect");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("positive-offset-outside")
            << QPointF(100, 75) << QPointF(100, 75) << QRectF(90, 10, 30, 20) << false;
        QTest::newRow("positive-offset-inside")
            << QPointF(100, 75) << QPointF(100, 75) << QRectF(10, 10, 20, 20) << true;
        QTest::newRow("negative-offset-outside")
            << QPointF(-20, -10) << QPointF(0, 0) << QRectF(0, 0, 10, 10) << false;
        QTest::newRow("negative-offset-inside")
            << QPointF(-20, -10) << QPointF(0, 0) << QRectF(30, 20, 10, 10) << true;
    }
    void jobBoundsIncludeWorkOffset() {
        QFETCH(QPointF, offset);
        QFETCH(QPointF, current);
        QFETCH(QRectF, rect);
        QFETCH(bool, allowed);
        Controller c;
        c.machinePosition = current;
        c.workOffset = offset;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("x", rect.x());
        d.setProperty("y", rect.y());
        d.setProperty("width", rect.width());
        d.setProperty("height", rect.height());
        QVERIFY(d.job().valid());
        const auto before = c.commands.size();
        s.start(d.job());
        if (allowed) {
            QVERIFY(s.busy());
            QTRY_VERIFY(c.commands.contains("M4 S800"));
        } else {
            QVERIFY(!s.busy());
            QTest::qWait(30);
            QCOMPARE(c.commands.size(), before);
        }
        s.close();
    }
    void helperFrameUsesOnlyLaserOffAbsoluteMovesAndKeepsPhysicalGates() {
        for (bool shifted : {false, true}) {
            Controller c;
            c.machinePosition = {10, 10};
            c.workOffset = shifted ? QPointF(190, 0) : QPointF(0, 0);
            GrblSession session;
            QVERIFY(session.open(c.port));
            QTRY_VERIFY(session.ready());
            requireReference(session);
            Document d;
            d.newProject();
            d.addShape("rect");
            d.setToolLayer(1);
            d.setProperty("x", 20.);
            d.setProperty("y", 30.);
            d.setProperty("width", 40.);
            d.setProperty("height", 10.);
            QVERIFY(d.job().moves.isEmpty());
            const auto bounds = d.framingBounds("t1");
            QVERIFY(bounds["valid"].toBool());
            const auto before = c.commands.size();
            session.frame(bounds);
            if (shifted) {
                QVERIFY(!session.busy());
                QTest::qWait(30);
                QCOMPARE(c.commands.size(), before);
            } else {
                const QStringList expected{"M5",
                                           "G21",
                                           "G90",
                                           "G54",
                                           "G0 X20.000 Y30.000",
                                           "G0 X60.000 Y30.000",
                                           "G0 X60.000 Y40.000",
                                           "G0 X20.000 Y40.000",
                                           "G0 X20.000 Y30.000"};
                QTRY_COMPARE(c.commands.mid(before), expected);
                for (const auto &command : c.commands.mid(before))
                    QVERIFY(!command.startsWith("M3") && !command.startsWith("M4"));
                QTRY_VERIFY(session.ready());
                QVERIFY(!session.firing);
            }
            session.close();
        }
    }
    void jogAndFrameRespectPhysicalBed() {
        Controller c;
        c.machinePosition = {199, 75};
        c.workOffset = {100, 75};
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        const auto before = c.commands.size();
        s.jog(2, 0);
        QVERIFY(!s.busy());
        s.frame({{"valid", true}, {"x", 190}, {"y", 10}, {"width", 5}, {"height", 5}});
        QVERIFY(!s.busy());
        QTest::qWait(30);
        QCOMPARE(c.commands.size(), before);
        s.jog(-1, 0);
        QTRY_VERIFY(c.commands.contains("$J=G91 G21 X-1.000 Y0.000 F600"));
    }
    void workPositionWithoutOffsetDoesNotAuthorizeMotion() {
        Controller c;
        c.respondToStatus = false;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(c.commands.contains("$G"));
        c.reply("<Idle|WPos:0,0,0|FS:0,0>\r\n");
        QTest::qWait(30);
        QVERIFY(!s.ready());
        c.report();
        QTRY_VERIFY(s.ready());
    }
    void idleRequiresFreshValidStatus_data() {
        QTest::addColumn<QByteArray>("status");
        QTest::newRow("invalid-position") << QByteArray("<Idle|MPos:nan,0,0|FS:0,0>");
        QTest::newRow("missing-position") << QByteArray("<Idle|FS:0,0>");
        QTest::newRow("nonzero-output") << QByteArray("<Idle|MPos:0,0,0|FS:0,20>");
        QTest::newRow("missing-output") << QByteArray("<Idle|MPos:0,0,0>");
        QTest::newRow("invalid-output") << QByteArray("<Idle|MPos:0,0,0|FS:0,nan>");
        QTest::newRow("active-input") << QByteArray("<Idle|MPos:0,0,0|FS:0,0|Pn:X>");
    }
    void idleRequiresFreshValidStatus() {
        QFETCH(QByteArray, status);
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.respondToStatus = false;
        c.reply(status + "\r\n");
        QTRY_VERIFY(!s.ready());
        Document d;
        d.addShape("rect");
        s.start(d.job());
        QTest::qWait(30);
        for (const auto &line : c.commands)
            QVERIFY(!line.startsWith("M4"));
        if (s.connected()) {
            c.report();
            QTRY_VERIFY(s.ready());
        } else
            QCOMPARE(status, QByteArray("<Idle|MPos:0,0,0|FS:0,20>"));
    }
    void interleavedSettingsAreReadAgain() {
        Controller c;
        c.interleaveSettingsOnce = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY_WITH_TIMEOUT(s.ready(), 4000);
        QCOMPARE(c.settingsRequests, 2);
    }
    void corruptedSettingsNeverAuthorizeOutput() {
        Controller c;
        c.interleaveSettingsOnce = c.wrongPower = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_COMPARE_WITH_TIMEOUT(c.settingsRequests, 2, 4000);
        QTest::qWait(50);
        QVERIFY(!s.ready());
        QVERIFY(!s.configurationValid());
    }
    void repeatedInterleavingClosesConnection() {
        Controller c;
        c.interleaveSettingsAlways = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_COMPARE(c.settingsRequests, 2);
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.ready());
    }
    void nonzeroMinimumPowerIsRejected() {
        Controller c;
        c.minimumPower = "10";
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(c.commands.contains("$G"));
        QTest::qWait(30);
        QVERIFY(!s.configurationValid());
        QVERIFY(!s.ready());
    }
    void interleavedStatusPreservesAcknowledgment() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        c.autoAck = false;
        QVERIFY(s.commands({"M5 S0", "G90"}));
        c.reply("ok<Idle|MPos:0,0,0|FS:0,0>\r\n");
        QTRY_VERIFY(c.commands.contains("G90"));
        c.reply("<Idle|MPos:0,0,0|FS:0,0>ok\r\n");
        QTRY_VERIFY(s.ready());
    }
    void delayedRunStatusDoesNotCancelRequestedPause() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.autoAck = false;
        Document d;
        d.addShape("rect");
        s.start(d.job());
        s.pause();
        c.reply("<Run|MPos:0,0,0|FS:0,0>\r\n");
        QTest::qWait(30);
        QVERIFY(s.paused);
        s.pause();
        QVERIFY(!s.paused);
        QTRY_VERIFY(c.realtime.contains('~'));
        s.stop();
    }
    void excessivePowerOverrideAbortsActiveJob() {
        Controller c;
        c.withOverrides = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.autoAck = false;
        Document d;
        d.addShape("rect");
        s.start(d.job());
        c.power = 150;
        c.status = "Run";
        c.report();
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.busy());
    }
    void transportLossDiscardsJobBeforeReconnect() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.autoAck = false;
        Document d;
        d.addShape("rect");
        s.start(d.job());
        QTRY_VERIFY(s.busy());
        c.disconnectPort();
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.ready());
        QVERIFY(!s.busy());
        Controller next;
        QVERIFY(s.open(next.port));
        QTRY_VERIFY(s.ready());
        for (const auto &line : next.commands)
            QVERIFY(!line.startsWith("G1") && !line.startsWith("M4"));
    }
    void failureDoesNotConsumeTrailingStartupBanner() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        c.reply("<Idle|MPos:0,0,0|FS:0,20>\r\nGrbl 1.3a ['$' for help]\r\n");
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.busy());
        QVERIFY(!s.ready());
        QCOMPARE(s.state, QString("Verbindungsfehler"));
    }
    void completionWaitsForValidPositionAndZeroOutput() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.status = "Run";
        Document d;
        d.addShape("rect");
        s.start(d.job());
        QTRY_COMPARE(s.progress, .99);
        c.respondToStatus = false;
        c.reply("<Idle|MPos:invalid,0,0|FS:0,0>\r\n");
        QTest::qWait(30);
        QVERIFY(s.busy());
        QVERIFY(s.progress < 1);
        c.status = "Idle";
        c.respondToStatus = true;
        c.report();
        QTRY_VERIFY(s.ready());
        QCOMPARE(s.progress, 1.);
    }
    void doorDuringJobDiscardsControllerQueue() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.autoAck = false;
        Document d;
        d.addShape("rect");
        s.start(d.job());
        QTRY_VERIFY(s.busy());
        c.status = "Door";
        c.report();
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.busy());
        QVERIFY(!s.ready());
        QTRY_VERIFY(c.realtime.contains(char(0x18)));
    }
    void bufferedCreditsAndOverrides() {
        Controller c;
        c.withOverrides = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY_WITH_TIMEOUT(s.ready(), 4000);
        requireReference(s);
        QTRY_VERIFY(s.overrideFeedback);
        QVERIFY(s.overrides(125, 70));
        QTRY_COMPARE(c.feed, 125);
        QTRY_COMPARE(c.power, 70);
        QTRY_COMPARE(s.feedOverride, 125);
        QVERIFY(!s.overrides(125, 110));
        c.autoAck = false;
        int before = c.commands.size();
        Document d;
        d.newProject();
        d.addShape("rect");
        s.start(d.job());
        QTRY_VERIFY(c.commands.size() > before + 1);
        QTest::qWait(50);
        int bytes = 0;
        for (int i = before; i < c.commands.size(); ++i)
            bytes += c.commands[i].toLatin1().size() + 1;
        QVERIFY(bytes <= 100);
        QVERIFY(s.busy());
        c.autoAck = true;
        s.stop();
        QTRY_VERIFY_WITH_TIMEOUT(s.ready(), 4000);
    }
    void streamAndCompletion() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY_WITH_TIMEOUT(s.ready(), 4000);
        requireReference(s);
        Document d;
        d.newProject();
        d.addShape("rect");
        c.status = "Run";
        s.start(d.job());
        QTRY_VERIFY_WITH_TIMEOUT(c.commands.contains("M5 S0"), 4000);
        QVERIFY(s.busy());
        QVERIFY(s.progress < 1);
        c.status = "Idle";
        c.report();
        QTRY_VERIFY(!s.busy());
        QCOMPARE(s.progress, 1.);
        QVERIFY(c.commands.contains("M4 S800"));
        s.close();
    }
    void machineFacadeSendsFrozenPlan() {
        Controller c;
        Document d;
        d.newProject();
        d.addShape("rect");
        Machine machine(&d);
        QVERIFY(machine.connectHardware(c.port));
        QTRY_VERIFY(machine.ready());
        requireReference(machine);
        QVERIFY(!machine.simulation());
        const int first = c.commands.size();
        const auto expected = d.job().gcode();
        QVERIFY(QMetaObject::invokeMethod(&machine, "start"));
        QTRY_VERIFY_WITH_TIMEOUT(!machine.busy(), 4000);
        QCOMPARE(c.commands.mid(first), expected);
        machine.useSimulator();
        QVERIFY(machine.simulation());
        QVERIFY(machine.ready());
    }
    void configGate() {
        Controller c;
        c.wrongPower = true;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(c.commands.contains("$G"));
        QTest::qWait(100);
        QVERIFY(!s.ready());
        Document d;
        s.start(d.job());
        QTest::qWait(50);
        for (auto line : c.commands)
            QVERIFY(!line.startsWith("M4"));
        s.close();
    }
    void pauseAndAbort() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        c.status = "Run";
        Document d;
        s.start(d.job());
        QVERIFY(s.busy());
        s.pause();
        QVERIFY(s.paused);
        s.pause();
        QVERIFY(!s.paused);
        s.stop();
        QVERIFY(!s.firing);
        QTRY_VERIFY_WITH_TIMEOUT(s.ready(), 4000);
        QVERIFY(!s.busy());
        s.close();
    }
    void finiteFocusAndAlarm() {
        Controller c;
        GrblSession s;
        QVERIFY(s.open(c.port));
        QTRY_VERIFY(s.ready());
        requireReference(s);
        s.focus(true);
        QVERIFY(s.firing);
        QTRY_VERIFY_WITH_TIMEOUT(!s.firing, 3000);
        QVERIFY(c.commands.contains("G4 P0.2"));
        QVERIFY(c.commands.contains("M5 S0"));
        c.reply("ALARM:1\r\n");
        QTRY_VERIFY(!s.connected());
        QVERIFY(!s.firing);
    }
};
QTEST_MAIN(GrblTest)
#include "grbl_test.moc"
