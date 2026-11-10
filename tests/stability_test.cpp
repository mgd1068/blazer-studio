#include "atomicfile.h"
#include "batchexport.h"
#include "document.h"
#include "imageprocessing.h"
#include "jobpreparation.h"
#include "machine.h"
#include "recovery.h"
#include <QElapsedTimer>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <stdexcept>
class StabilityTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString put(QString name, QByteArray bytes) {
        auto path = temp.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return {};
        return path;
    }
    QByteArray read(QString path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return f.readAll();
    }
  private slots:
    void asynchronousMatchesReferenceAndOwnsSnapshot() {
        Document d;
        d.newProject();
        d.addShape("rect");
        auto input = d.jobInput();
        const auto expected = JobPlanner::build(input).gcode();
        JobPreparation preparation;
        preparation.submit(input);
        d.setProperty("width", 17.);
        QVERIFY(!preparation.plan().valid());
        QTRY_COMPARE_WITH_TIMEOUT(preparation.state(), QString("ready"), 5000);
        QCOMPARE(preparation.plan().gcode(), expected);
        QVERIFY(!preparation.plan().commands.isEmpty());
        QVERIFY(preparation.plan().gcode() != d.job().gcode());
    }
    void cancellationKeepsEventLoopResponsiveAndDropsOldGenerations() {
        Document d;
        d.newProject();
        d.addShape("rect");
        std::atomic_int started = 0;
        JobPreparation preparation(nullptr, [&](const JobInput &input, const Cancellation &cancel) {
            const auto n = ++started;
            if (n == 1)
                while (!isCancelled(cancel))
                    QThread::msleep(1);
            return JobPlanner::build(input, cancel);
        });
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(2);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start();
        preparation.submit(d.jobInput());
        QTRY_VERIFY(started.load() > 0);
        QTest::qWait(30);
        QVERIFY(ticks >= 3);
        d.setProperty("width", 21.);
        preparation.submit(d.jobInput());
        d.setProperty("width", 31.);
        preparation.submit(d.jobInput());
        QVERIFY(!preparation.plan().valid());
        QTRY_COMPARE_WITH_TIMEOUT(preparation.state(), QString("ready"), 5000);
        QCOMPARE(preparation.plan().gcode(), d.job().gcode());
        QVERIFY(started.load() <= 3);
        preparation.cancel();
        QVERIFY(!preparation.plan().valid());
        QCOMPARE(preparation.state(), QString("cancelled"));
    }
    void shutdownCancelsRunningWorker() {
        Document d;
        std::atomic_bool started = false;
        QElapsedTimer elapsed;
        auto p = std::make_unique<JobPreparation>(
            nullptr, [&](const JobInput &, const Cancellation &cancel) {
                started = true;
                while (!isCancelled(cancel))
                    QThread::msleep(1);
                return JobPlan{};
            });
        p->submit(d.jobInput());
        QTRY_VERIFY(started.load());
        elapsed.start();
        p.reset();
        QVERIFY(elapsed.elapsed() < 1000);
    }
    void failureNeverPublishesRunnablePartialJob() {
        Document d;
        JobPreparation p(nullptr, [](const JobInput &, const Cancellation &) -> JobPlan {
            throw std::runtime_error("fixture");
        });
        p.submit(d.jobInput());
        QTRY_COMPARE(p.state(), QString("failed"));
        QVERIFY(!p.plan().valid());
        QVERIFY(p.plan().moves.isEmpty());
        QVERIFY(!p.plan().errors.isEmpty());
    }
    void pageUndoAndExportUseOnlyCurrentResult() {
        Document d;
        d.newProject();
        d.addShape("rect");
        d.setProperty("width", 23.);
        d.setBackgroundPlanning(true);
        Machine machine(&d);
        machine.start();
        QVERIFY(!machine.busy());
        auto output = QUrl::fromLocalFile(temp.filePath("pending.gcode"));
        QVERIFY(!d.exportGcode(output));
        QVERIFY(!QFile::exists(output.toLocalFile()));
        QTRY_VERIFY(d.job().valid());
        QVERIFY(d.addPage("Blatt zwei"));
        d.addShape("ellipse");
        QVERIFY(!d.job().valid());
        QTRY_VERIFY(d.job().valid());
        auto second = d.job().gcode();
        d.activatePage(0);
        QVERIFY(!d.job().valid());
        QTRY_VERIFY(d.job().valid());
        QVERIFY(d.job().gcode() != second);
        d.select(0);
        d.setProperty("width", 33.);
        d.undo();
        QTRY_VERIFY(d.job().valid());
        QCOMPARE(d.job().gcode(), JobPlanner::build(d.jobInput()).gcode());
        QVERIFY(d.exportGcode(output));
        QCOMPARE(read(output.toLocalFile()), (d.job().gcode().join('\n') + '\n').toUtf8());
    }
    void rasterCancellationAndThreadSafeSvgSnapshot() {
        auto flag = std::make_shared<std::atomic_bool>(true);
        QString error;
        QImage source(1024, 1024, QImage::Format_ARGB32);
        source.fill(Qt::black);
        QVERIFY(ImageProcessing::apply(source, {}, error, flag).isNull());
        QVERIFY(!error.isEmpty());
        Document d;
        auto job = JobPlanner::build(d.jobInput(), flag);
        QVERIFY(job.cancelled);
        QVERIFY(job.moves.isEmpty());
        QVERIFY(job.gcode().isEmpty());
        auto path = put("worker.svg", "<svg xmlns='http://www.w3.org/2000/svg' width='10mm' "
                                      "height='10mm'><rect width='10' height='10'/></svg>");
        d.newProject();
        QVERIFY(d.importSvg(QUrl::fromLocalFile(path)));
        auto snapshot = d.jobInput();
        QVERIFY(snapshot.items[0].asset);
        QVERIFY(!snapshot.items[0].asset->renderer);
        JobPreparation p;
        p.submit(snapshot);
        QTRY_COMPARE(p.state(), QString("ready"));
        QCOMPARE(p.plan().gcode(), d.job().gcode());
        snapshot.items[0].layer = 99;
        QVERIFY(!JobPlanner::build(snapshot).valid());
    }
    void realLargeRasterHeartbeatAndCancellation() {
        QImage image(2048, 2048, QImage::Format_ARGB32);
        image.fill(Qt::black);
        auto path = temp.filePath("large.png");
        QVERIFY(image.save(path));
        Document d;
        d.newProject();
        QVERIFY(d.importImage(QUrl::fromLocalFile(path)));
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start();
        d.setBackgroundPlanning(true);
        QTRY_COMPARE(d.planningState(), QString("running"));
        d.cancelPlanning();
        QTRY_VERIFY_WITH_TIMEOUT(!d.planningBusy(), 3000);
        QVERIFY(!d.job().valid());
        QVERIFY(ticks > 0);
        d.retryPlanning();
        QTRY_VERIFY_WITH_TIMEOUT(d.job().valid(), 15000);
        QCOMPARE(d.job().previews[d.items()[0].id].size(), image.size());
    }
    void lockedProjectAndQuotaFailurePreserveFiles() {
        Document d;
        d.newProject();
        d.addShape("rect");
        auto path = temp.filePath("protected.blazer");
        auto url = QUrl::fromLocalFile(path);
        QVERIFY(d.save(url));
        d.setProperty("width", 19.);
        QVERIFY(d.save(url));
        const auto original = read(path), backup = read(path + ".bak");
        QLockFile lock(path + ".lock");
        QVERIFY(lock.tryLock(0));
        d.setProperty("width", 29.);
        QVERIFY(!d.save(url));
        QVERIFY(d.dirty());
        QCOMPARE(read(path), original);
        QCOMPARE(read(path + ".bak"), backup);
        lock.unlock();
        QProcess child;
        child.start(QStringLiteral(BLAZER_STORAGE_PROBE), {"--quota", path});
        QVERIFY(child.waitForFinished());
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(read(path), original);
        QCOMPARE(read(path + ".bak"), backup);
    }
    void recoveryLockAndFallbackPreserveLastGoodCopy() {
        Document d;
        d.newProject();
        Recovery a(&d, temp.filePath("recovery-lock"));
        QVERIFY(a.writable());
        d.addShape("rect");
        d.setProperty("width", 20.);
        QVERIFY(a.save());
        d.setProperty("width", 30.);
        QVERIFY(a.save());
        const auto previous = read(a.location() + ".previous");
        Recovery b(&d, temp.filePath("recovery-lock"));
        QVERIFY(!b.writable());
        b.clear();
        QVERIFY(!b.save());
        QVERIFY(QFile::exists(a.location()));
        put("recovery-lock/autosave.blazer", "broken");
        QVERIFY(a.restore());
        QCOMPARE(d.items()[0].bounds.width(), 20.);
        QVERIFY(a.save());
        QCOMPARE(read(a.location() + ".previous"), previous);
        Document restored;
        QVERIFY(restored.restoreBytes(read(a.location())));
        QCOMPARE(restored.items()[0].bounds.width(), 20.);
    }
    void killedProcessRecoversAndReleasesItsLock() {
        auto folder = temp.filePath("crash-recovery");
        QProcess child;
        child.start(QStringLiteral(BLAZER_STORAGE_PROBE), {"--hold", folder});
        QVERIFY(child.waitForReadyRead());
        QVERIFY(child.readAllStandardOutput().contains("ready"));
        child.kill();
        QVERIFY(child.waitForFinished());
        Document restored;
        Recovery recovery(&restored, folder);
        QVERIFY(recovery.writable());
        QVERIFY(recovery.available());
        QVERIFY(recovery.restore());
        QCOMPARE(restored.items()[0].bounds.width(), 37.);
        QVERIFY(restored.dirty());
    }
    void malformedOpenAndRestoreAreAtomic() {
        Document d;
        d.newProject();
        d.addShape("rect");
        const auto original = d.projectBytes();
        for (int i = 0; i < 64; ++i) {
            auto bad = original.left(i);
            auto path = put("broken.blazer", bad);
            QVERIFY(!d.open(QUrl::fromLocalFile(path)));
            QCOMPARE(d.projectBytes(), original);
            QVERIFY(!d.restoreBytes(bad));
            QCOMPARE(d.projectBytes(), original);
        }
    }
    void batchExportIsDetachedAndNeverOverwrites() {
        Document d;
        d.newProject();
        d.addShape("text");
        d.setProperty("text", "{name}-{number}");
        auto csv = put("batch.csv", "name\nAlice\nBob\n");
        auto folder = temp.filePath("batch-output");
        QVERIFY(QDir().mkpath(folder));
        const auto before = d.projectBytes();
        d.setBackgroundPlanning(true);
        QVERIFY(d.batch(QUrl::fromLocalFile(csv), QUrl::fromLocalFile(folder), {}));
        QTRY_VERIFY_WITH_TIMEOUT(!d.batchBusy(), 10000);
        QCOMPARE(d.projectBytes(), before);
        QVERIFY(QFile::exists(folder + "/job-0002.gcode"));
        auto original = read(folder + "/job-0001.gcode");
        auto result = BatchExport::run(d.jobInput(), QUrl::fromLocalFile(csv),
                                       QUrl::fromLocalFile(folder), {});
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(folder + "/job-0001.gcode"), original);
        auto cancelled = std::make_shared<std::atomic_bool>(true);
        auto cancelFolder = temp.filePath("cancel-output");
        QVERIFY(QDir().mkpath(cancelFolder));
        result = BatchExport::run(d.jobInput(), QUrl::fromLocalFile(csv),
                                  QUrl::fromLocalFile(cancelFolder), {}, cancelled);
        QVERIFY(result.cancelled);
        QVERIFY(QDir(cancelFolder).entryList({"*.gcode"}, QDir::Files).isEmpty());
    }
};
QTEST_MAIN(StabilityTest)
#include "stability_test.moc"
