#include "document.h"
#include "grbl.h"
#include "machine.h"
#include <QSocketNotifier>
#include <QtTest>
#include <fcntl.h>
#include <pty.h>
#include <unistd.h>
class Controller : public QObject {
  public:
    int master = -1;
    QString port;
    QByteArray buffer;
    QStringList commands;
    QString status = "Idle";
    bool banner = false, wrongPower = false, autoAck = true, withOverrides = false;
    bool respondToStatus = true, interleaveSettingsOnce = false;
    bool interleaveSettingsAlways = false;
    QByteArray minimumPower = "0";
    int settingsRequests = 0;
    QSocketNotifier *notifier = nullptr;
    QByteArray realtime;
    int feed = 100, power = 100;
    Controller() {
        int slave;
        char name[256];
        if (openpty(&master, &slave, name, nullptr, nullptr))
            qFatal("openpty failed");
        port = name;
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
                        continue;
                    }
                    if (c == '?') {
                        if (respondToStatus)
                            report();
                        continue;
                    }
                    if (c == 0x18) {
                        realtime.append(c);
                        buffer.clear();
                        status = "Idle";
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
                        if (line == "$I")
                            reply("[VER:1.3a.20211103:]\r\n");
                        if (line == "$G")
                            reply("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\n");
                        if (line == "$$") {
                            ++settingsRequests;
                            QByteArray settings = wrongPower ? "$30=255\r\n" : "$30=1000\r\n";
                            if (interleaveSettingsAlways ||
                                (interleaveSettingsOnce && settingsRequests == 1))
                                settings = "$30=1000<Idle|MPos:0,0,0|WCO:0,0,0|FS:0,0>\r\n";
                            reply(settings + "$31=" + minimumPower + "\r\n$32=1\r\n$22=1\r\n$13=0\r\n"
                                             "$130=200\r\n$131=150\r\n");
                        }
                        if (autoAck)
                            reply("ok\r\n");
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
        reply(("<" + status + "|MPos:0,0,0|WCO:0,0,0|FS:0,0" +
               (withOverrides ? "|Ov:" + QString::number(feed) + ",100," + QString::number(power)
                              : QString()) +
               ">\r\n")
                  .toLatin1());
    }
};
class GrblTest : public QObject {
    Q_OBJECT
  private slots:
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
