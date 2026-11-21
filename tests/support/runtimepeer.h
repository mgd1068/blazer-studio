#pragma once
// Test-only synchronous control wrapper. The peer itself is a separate process;
// no callback/event processing here is required while the GUI is deliberately blocked.
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <time.h>

namespace RuntimeTestSupport {
class RuntimePeer final {
  public:
    struct Reply {
        bool ok = false;
        QJsonObject object;
        QString error;
    };
    struct Exit {
        bool stopped = true;
        bool killed = false;
        int code = 0;
    };
    static constexpr qsizetype frameLimit = 65536;
    static constexpr int defaultTimeoutMs = 3000;
    RuntimePeer() = default;
    ~RuntimePeer() { shutdown(); }
    Q_DISABLE_COPY_MOVE(RuntimePeer)

    static qint64 monotonicNs() {
        timespec value{};
        if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
            return -1;
        return qint64(value.tv_sec) * 1000000000LL + value.tv_nsec;
    }
    QString port() const { return endpoint; }
    qint64 childPid() const { return pid; }
    bool running() const { return process.state() != QProcess::NotRunning; }
    QString diagnostic() const { return QString::fromUtf8(errors.right(2048)); }

    Reply start(const QString &scriptPath, int timeoutMs = defaultTimeoutMs) {
        if (!ownerThread() || running() || !validTimeout(timeoutMs))
            return failure("Invalid test peer start state/timeout/thread.");
        if (!QFileInfo(scriptPath).isFile())
            return failure("Test peer script is not a regular file.");
        const auto python = QStandardPaths::findExecutable("python3");
        if (python.isEmpty())
            return failure("python3 is unavailable for the test peer.");
        input.clear(); errors.clear(); poisoned.clear(); endpoint.clear();
        nextId = 0; pid = 0; errorBytes = 0; lastExit = {};
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.setProgram(python);
        process.setArguments({"-u", QFileInfo(scriptPath).absoluteFilePath()});
        QElapsedTimer budget;
        budget.start();
        process.start();
        if (!process.waitForStarted(timeoutMs))
            return abort("Test peer could not start.");
        pid = process.processId();
        auto ready = readObject(std::max(1, timeoutMs - int(budget.elapsed())));
        if (!ready.ok)
            return ready;
        const auto &value = ready.object;
        static const QRegularExpression syntheticPath("^/dev/pts/[0-9]{1,10}$");
        if (value.value("ready") != QJsonValue(true) ||
            value.value("syntheticOnly") != QJsonValue(true) ||
            !value.value("pid").isDouble() || value.value("pid").toInteger(-1) != pid ||
            !value.value("port").isString() ||
            !syntheticPath.match(value.value("port").toString()).hasMatch())
            return abort("Invalid synthetic peer readiness/PID/PTY provenance.");
        endpoint = value.value("port").toString();
        return ready;
    }
    Reply control(QJsonObject request, int timeoutMs = defaultTimeoutMs) {
        if (!ownerThread() || !validTimeout(timeoutMs) || !running() || endpoint.isEmpty())
            return failure("Test peer control unavailable/invalid timeout/thread.");
        if (!poisoned.isEmpty())
            return failure(poisoned);
        const auto action = request.value("action");
        if (!action.isString() || action.toString().isEmpty() || action.toString().size() > 64)
            return failure("Invalid test peer control action.");
        if (nextId >= 9007199254740991LL)
            return failure("Test peer control sequence exhausted.");
        const auto id = ++nextId;
        request.insert("id", id); // A caller cannot forge/collide a correlation ID.
        auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (bytes.size() > frameLimit)
            return failure("Test peer control exceeds frame budget.");
        QElapsedTimer budget;
        budget.start();
        if (process.write(bytes) != bytes.size())
            return abort("Test peer control write failed.");
        while (process.bytesToWrite() > 0) {
            const int remaining = timeoutMs - int(budget.elapsed());
            if (remaining <= 0 || !process.waitForBytesWritten(remaining))
                return abort("Test peer control write timed out.");
        }
        const int remaining = timeoutMs - int(budget.elapsed());
        if (remaining <= 0)
            return abort("Test peer control deadline elapsed.");
        auto response = readObject(remaining);
        if (!response.ok)
            return response;
        if (!response.object.value("id").isDouble() || response.object.value("id").toInteger(-1) != id)
            return abort("Test peer response correlation mismatch.");
        return response;
    }
    Reply schedule(int delayMs, const QJsonObject &operation, int timeoutMs = defaultTimeoutMs) {
        if (delayMs < 0 || delayMs > 10000 ||
            !QStringList{"set", "reply", "report", "disconnect"}.contains(operation.value("action").toString()))
            return failure("Invalid bounded test peer schedule.");
        auto result = control({{"action", "schedule"}, {"delayMs", delayMs}, {"operation", operation}}, timeoutMs);
        if (result.ok && result.object.value("accepted") != QJsonValue(true))
            return abort("Test peer did not accept scheduled operation.");
        return result;
    }
    Reply snapshot(int timeoutMs = defaultTimeoutMs) {
        auto result = control({{"action", "snapshot"}}, timeoutMs);
        if (result.ok && (!result.object.value("events").isArray() ||
                          !result.object.value("counts").isObject()))
            return abort("Invalid test peer snapshot.");
        return result;
    }
    Exit shutdown(int graceMs = 500, int killMs = 1000) {
        // QProcess is used only by its owning test thread. Never kill a PID from
        // JSON; kill() always targets exactly this QProcess child.
        if (!ownerThread())
            return {false, false, -1};
        graceMs = std::clamp(graceMs, 0, 3000);
        killMs = std::clamp(killMs, 1, 3000);
        if (running()) {
            process.closeWriteChannel(); // Peer exits cleanly on control EOF.
            if (!process.waitForFinished(graceMs)) {
                lastExit.killed = true;
                process.kill();
                process.waitForFinished(killMs);
            }
        }
        lastExit.stopped = !running();
        lastExit.code = process.exitCode();
        endpoint.clear();
        return lastExit;
    }

  private:
    QProcess process;
    QByteArray input, errors;
    QString endpoint, poisoned;
    qint64 pid = 0, nextId = 0;
    qsizetype errorBytes = 0;
    Exit lastExit;
    bool ownerThread() const { return process.thread() == QThread::currentThread(); }
    static bool validTimeout(int value) { return value >= 1 && value <= 10000; }
    static Reply failure(const QString &message) { return {false, {}, message}; }
    Reply abort(const QString &message) {
        poisoned = message;
        shutdown(50, 1000);
        return failure(message);
    }
    Reply readObject(int timeoutMs) {
        QElapsedTimer elapsed;
        elapsed.start();
        for (;;) {
            // Bounded reads; never accumulate freeform/unlimited child logs.
            process.setReadChannel(QProcess::StandardError);
            while (process.bytesAvailable() > 0) {
                const auto chunk = process.read(4096);
                errorBytes += chunk.size();
                errors.append(chunk);
                if (errors.size() > 4096)
                    errors = errors.right(4096);
                if (errorBytes > frameLimit) {
                    process.setReadChannel(QProcess::StandardOutput);
                    return abort("Test peer stderr exceeded budget.");
                }
            }
            process.setReadChannel(QProcess::StandardOutput);
            input.append(process.read(frameLimit + 1 - input.size()));
            if (input.size() > frameLimit)
                return abort("Test peer stdout exceeded frame budget.");
            const auto newline = input.indexOf('\n');
            if (newline >= 0) {
                const auto line = input.first(newline);
                input.remove(0, newline + 1);
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(line, &error);
                if (error.error != QJsonParseError::NoError || !document.isObject())
                    return abort("Malformed test peer JSON frame.");
                return {true, document.object(), {}};
            }
            if (!running())
                return abort("Test peer exited before a complete response.");
            const int remaining = timeoutMs - int(elapsed.elapsed());
            if (remaining <= 0)
                return abort("Test peer JSON response timed out.");
            process.waitForReadyRead(std::min(remaining, 50));
        }
    }
};
} // namespace RuntimeTestSupport
