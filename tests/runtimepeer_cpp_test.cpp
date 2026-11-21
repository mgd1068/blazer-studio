#include "support/runtimepeer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <chrono>
#include <thread>
using RuntimeTestSupport::RuntimePeer;

class RuntimePeerCppTest : public QObject {
    Q_OBJECT
    const QString script = QStringLiteral(BLAZER_TEST_SOURCE_DIR "/tests/support/runtime_pty_peer.py");
    static QString malformedPeer(QTemporaryDir &directory, const QByteArray &body) {
        const auto path = directory.filePath("protocol_failure.py");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        const auto code = QByteArray("import json,os,sys,time\n") + body;
        return file.write(code) == code.size() ? path : QString();
    }
    static QByteArray validReady() {
        // Failure harness only: never opens a PTY/serial endpoint. Production
        // runtime cases use the actual Root process fixture, not this script.
        return "print(json.dumps(dict(ready=True,syntheticOnly=True,pid=os.getpid(),port='/dev/pts/999999999')),flush=True)\n";
    }
  private slots:
    void readinessCorrelatedControlsAndCleanEof() {
        RuntimePeer peer;
        auto ready = peer.start(script);
        QVERIFY2(ready.ok, qPrintable(ready.error));
        QVERIFY(peer.childPid() > 0);
        QVERIFY(peer.childPid() != QCoreApplication::applicationPid());
        QVERIFY(peer.port().startsWith("/dev/pts/"));
        auto set = peer.control({{"action", "set"}, {"id", 999}, {"values", QJsonObject{{"state", "Run"}}}});
        QVERIFY2(set.ok, qPrintable(set.error));
        QCOMPARE(set.object.value("id").toInteger(), 1);
        auto snapshot = peer.snapshot();
        QVERIFY2(snapshot.ok, qPrintable(snapshot.error));
        QCOMPARE(snapshot.object.value("id").toInteger(), 2);
        QCOMPARE(snapshot.object.value("state").toString(), QString("Run"));
        QCOMPARE(snapshot.object.value("counts").toObject().value("configured").toInt(), 1);
        const auto result = peer.shutdown();
        QVERIFY(result.stopped);
        QVERIFY(!result.killed);
        QCOMPARE(result.code, 0);
        QVERIFY(peer.port().isEmpty());
        QVERIFY(!peer.control({{"action", "report"}}).ok);
        QVERIFY(peer.shutdown().stopped);
    }
    void childKeepsRunningWhileGuiIsBlocked_data() {
        QTest::addColumn<int>("blockMs");
        QTest::newRow("400ms") << 400;
        QTest::newRow("2000ms") << 2000;
    }
    void childKeepsRunningWhileGuiIsBlocked() {
        QFETCH(int, blockMs);
        RuntimePeer peer;
        const auto ready = peer.start(script);
        QVERIFY2(ready.ok, qPrintable(ready.error));
        QVERIFY(peer.control({{"action", "set"}, {"values", QJsonObject{{"state", "Run"}}}}).ok);
        QVERIFY(peer.schedule(150, {{"action", "report"}}).ok);
        QVERIFY(peer.schedule(250, {{"action", "set"}, {"values", QJsonObject{{"state", "Hold"}}}}).ok);
        QVERIFY(peer.schedule(275, {{"action", "report"}}).ok);
        bool guiTimerDelivered = false;
        QObject timerContext; // Cancels the capture even if a QVERIFY returns early.
        QTimer::singleShot(20, &timerContext, [&] { guiTimerDelivered = true; });
        const auto begin = RuntimePeer::monotonicNs();
        QVERIFY(begin > 0);
        // Deliberate GUI/event-loop starvation. Do not replace by qWait/processEvents.
        std::this_thread::sleep_for(std::chrono::milliseconds(blockMs));
        const auto end = RuntimePeer::monotonicNs();
        QVERIFY(!guiTimerDelivered);
        QVERIFY(end - begin >= qint64(blockMs) * 1000000);
        const auto snapshot = peer.snapshot();
        QVERIFY2(snapshot.ok, qPrintable(snapshot.error));
        QStringList states;
        for (const auto &value : snapshot.object.value("events").toArray()) {
            const auto event = value.toObject();
            if (event.value("kind") != "statusReply")
                continue;
            const auto at = event.value("atNs").toInteger();
            QVERIFY2(at > begin && at < end, "Child status must be independently timestamped inside the GUI block");
            states.append(event.value("value").toString());
        }
        QCOMPARE(states, QStringList({"Run", "Hold"}));
        QTRY_VERIFY_WITH_TIMEOUT(guiTimerDelivered, 500);
    }
    void scheduledHangupOccursBeforeGuiReturns() {
        RuntimePeer peer;
        QVERIFY(peer.start(script).ok);
        QVERIFY(peer.schedule(100, {{"action", "disconnect"}}).ok);
        const auto begin = RuntimePeer::monotonicNs();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto end = RuntimePeer::monotonicNs();
        const auto snapshot = peer.snapshot();
        QVERIFY2(snapshot.ok, qPrintable(snapshot.error));
        QCOMPARE(snapshot.object.value("counts").toObject().value("disconnected").toInt(), 1);
        bool found = false;
        for (const auto &value : snapshot.object.value("events").toArray()) {
            const auto event = value.toObject();
            if (event.value("kind") == "disconnected") {
                found = true;
                QVERIFY(event.value("atNs").toInteger() > begin);
                QVERIFY(event.value("atNs").toInteger() < end);
            }
        }
        QVERIFY(found);
    }
    void boundsInvalidRequestsAndPeerFailureStayBounded() {
        RuntimePeer peer;
        QVERIFY(!peer.start("/missing-fixture").ok);
        QVERIFY(!peer.control({{"action", "report"}}).ok);
        QVERIFY(peer.start(script).ok);
        QVERIFY(!peer.schedule(-1, {{"action", "report"}}).ok);
        QVERIFY(!peer.schedule(10001, {{"action", "report"}}).ok);
        QVERIFY(!peer.schedule(1, {{"action", "quit"}}).ok);
        QVERIFY(!peer.control({{"action", "report"}}, 0).ok);
        QVERIFY(!peer.control({{"action", "reply"}, {"data", QString(70000, 'x')}}).ok);
        QVERIFY(peer.snapshot().ok); // Locally refused requests do not poison the child.
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = peer.control({{"action", "set"}, {"values", QJsonObject{{"state", "invalid"}}}});
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(elapsed.elapsed() < 3500);
        QVERIFY(peer.shutdown().stopped);
    }
    void journalIsBoundedAndRepeatedLifetimeExitsCleanly() {
        RuntimePeer peer;
        for (int lifetime = 0; lifetime < 3; ++lifetime) {
            QVERIFY(peer.start(script).ok);
            for (int i = 0; i < 280; ++i)
                QVERIFY(peer.control({{"action", "set"}, {"values", QJsonObject{{"state", "Idle"}}}}).ok);
            const auto snapshot = peer.snapshot();
            QVERIFY(snapshot.ok);
            QCOMPARE(snapshot.object.value("events").toArray().size(), 256);
            QCOMPARE(snapshot.object.value("counts").toObject().value("configured").toInt(), 280);
            const auto result = peer.shutdown();
            QVERIFY(result.stopped && !result.killed);
            QCOMPARE(result.code, 0);
        }
    }
    void forgedReadinessAndCorrelationAreRejected() {
        for (const auto &body : {
                 QByteArray("print(json.dumps(dict(ready=True,syntheticOnly=True,pid=os.getpid()+1,port='/dev/pts/123')),flush=True)\ntime.sleep(10)\n"),
                 QByteArray("print(json.dumps(dict(ready=True,syntheticOnly=True,pid=os.getpid(),port='/dev/ttyUSB0')),flush=True)\ntime.sleep(10)\n"),
                 QByteArray("print('not-json',flush=True)\ntime.sleep(10)\n"),
                 QByteArray("print(json.dumps(dict(ready=True,syntheticOnly=False,pid=os.getpid(),port='/dev/pts/123')),flush=True)\ntime.sleep(10)\n")}) {
            QTemporaryDir directory;
            RuntimePeer peer;
            QElapsedTimer elapsed;
            elapsed.start();
            QVERIFY(!peer.start(malformedPeer(directory, body)).ok);
            QVERIFY(!peer.running());
            QVERIFY(elapsed.elapsed() < 2000);
        }
        QTemporaryDir directory;
        RuntimePeer peer;
        const auto path = malformedPeer(directory, validReady() +
            "request=json.loads(sys.stdin.readline())\nprint(json.dumps(dict(id=request['id']+1)),flush=True)\ntime.sleep(10)\n");
        QVERIFY(peer.start(path).ok);
        const auto result = peer.snapshot();
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("correlation"));
        QVERIFY(!peer.running());
    }
    void shutdownKillsOnlyItsUncooperativeChildAfterGrace() {
        QTemporaryDir directory;
        RuntimePeer peer;
        QVERIFY(peer.start(malformedPeer(directory, validReady() + "time.sleep(10)\n")).ok);
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = peer.shutdown(30, 1000);
        QVERIFY(result.stopped && result.killed);
        QVERIFY(elapsed.elapsed() < 1500);
        QVERIFY(!peer.running());
    }
    void hungAndOversizedRepliesCannotHangTheTest() {
        for (const auto &body : {QByteArray("sys.stdin.readline()\ntime.sleep(10)\n"),
                                QByteArray("request=json.loads(sys.stdin.readline())\nprint(json.dumps(dict(id=request['id'],data='x'*70000)),flush=True)\ntime.sleep(10)\n"),
                                QByteArray("sys.stdin.readline()\nsys.stderr.write('x'*100000)\nsys.stderr.flush()\ntime.sleep(10)\n")}) {
            QTemporaryDir directory;
            RuntimePeer peer;
            QVERIFY(peer.start(malformedPeer(directory, validReady() + body)).ok);
            QElapsedTimer elapsed;
            elapsed.start();
            const auto result = peer.control({{"action", "snapshot"}}, 100);
            QVERIFY(!result.ok);
            QVERIFY(elapsed.elapsed() < 1800);
            QVERIFY(!peer.running());
        }
    }
};
QTEST_MAIN(RuntimePeerCppTest)
#include "runtimepeer_cpp_test.moc"
