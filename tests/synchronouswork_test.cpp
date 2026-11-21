#include "document.h"
#include "artlibrarycontroller.h"
#include "artlibrary.h"
#include "machine.h"
#include "recovery.h"
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class SynchronousWorkTest : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
    static QByteArray read(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }
    static void prepare(Document &d) {
        d.newProject();
        d.addShape("rect");
    }
  private slots:
    void additiveGuardsAndDestroyedOwner() {
        Document d;
        bool active = false;
        d.addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        QPointer<QObject> owner = new QObject;
        d.addSynchronousWorkGuard([owner] { return owner ? QString("second") : QString(); });
        QCOMPARE(d.synchronousWorkBlockReason(), QString("second"));
        delete owner.data();
        QVERIFY(d.synchronousWorkBlockReason().isEmpty());
        active = true;
        QCOMPARE(d.synchronousWorkBlockReason(), QString("active"));
        d.addSynchronousWorkGuard([] { return QString(); });
        QCOMPARE(d.synchronousWorkBlockReason(), QString("active"));
        active = false;
        d.addSynchronousWorkGuard([]() -> QString { throw 7; });
        QVERIFY(!d.synchronousWorkBlockReason().isEmpty());
    }
    void heavyEntryPointsAreAtomicAndLightEditingRemainsAvailable() {
        QTemporaryDir dir;
        Document d;
        prepare(d);
        const auto before = d.projectBytes();
        const auto history = d.historyPosition();
        const auto fileUrl = d.fileUrl();
        const auto file = QUrl::fromLocalFile(dir.filePath("sentinel.blazer"));
        write(file.toLocalFile(), "unchanged");
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("physical lease") : QString(); });
        QVERIFY(!d.save(file));
        QVERIFY(!d.open(file));
        QVERIFY(!d.saveTemplate(file));
        QVERIFY(!d.newFromTemplate(file));
        QVERIFY(!d.importFile(file));
        QVERIFY(!d.importSvg(file));
        QVERIFY(!d.importImage(file));
        QVERIFY(!d.importRelief(file));
        QVERIFY(!d.importModel(file));
        QVERIFY(!d.importProject(file));
        QVERIFY(!d.importNativeGeometry(file, QString(64, 'a'), false));
        QVERIFY(!d.previewNativeImport(file).value("canImportGeometry").toBool());
        QVERIFY(!d.exportSvg(file));
        QVERIFY(!d.exportGcode(file));
        QVERIFY(!d.copySelectionToClipboard());
        QVERIFY(!d.pasteFromClipboard());
        QVERIFY(!d.restoreBytes(before));
        QVERIFY(!d.insertArtGeometry({}, 0, 0));
        QVERIFY(!d.copyPage(0, "copy"));
        QVERIFY(!d.setNode(0, 10, 10));
        for (const auto &action : {"trace", "union", "offset", "nest", "convert", "textOnPath", "duplicateGrid"})
            QVERIFY(!d.command(action));
        QCOMPARE(d.historyPosition(), history);
        QCOMPARE(d.fileUrl(), fileUrl);
        QCOMPARE(read(file.toLocalFile()), QByteArray("unchanged"));
        QVERIFY(d.projectBytes().isEmpty());
        active = false;
        QCOMPARE(d.projectBytes(), before);
        active = true;
        d.setProperty("x", 42.);
        QCOMPARE(d.items().front().bounds.x(), 42.);
        QVERIFY(d.setMetadata("next design", "light edits allowed"));
        active = false;
        QVERIFY(d.save(QUrl::fromLocalFile(dir.filePath("next.blazer"))));
    }
    void pdfProcessTimerReproducerAndGuardPreventsLaunch() {
        QTemporaryDir dir;
        const auto script = dir.filePath("inkscape");
        const auto marker = dir.filePath("launched");
        // This fake process accesses only temporary fixtures. No Inkscape or devices.
        write(script, "#!/bin/sh\n/bin/touch '" + marker.toUtf8() + "'\n/bin/sleep 0.30\nexit 1\n");
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto oldPath = qgetenv("PATH");
        const auto cleanup = qScopeGuard([&] { qputenv("PATH", oldPath); });
        qputenv("PATH", dir.path().toUtf8() + ":" + oldPath);
        const auto pdf = QUrl::fromLocalFile(dir.filePath("input.pdf"));
        write(pdf.toLocalFile(), "%PDF-1.0\n");
        Document d;
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("physical lease") : QString(); });
        bool scheduledCallback = false;
        QTimer::singleShot(20, &d, [&] { scheduledCallback = true; });
        QVERIFY(!d.importFile(pdf));
        QVERIFY(!QFile::exists(marker));
        QTRY_VERIFY_WITH_TIMEOUT(scheduledCallback, 500);
        active = false;
        scheduledCallback = false;
        QTimer::singleShot(20, &d, [&] { scheduledCallback = true; });
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(!d.importFile(pdf));
        const auto blockedMs = elapsed.elapsed();
        QVERIFY(QFile::exists(marker));
        QVERIFY2(blockedMs >= 200, qPrintable(QString::number(blockedMs)));
        QVERIFY(!scheduledCallback); // GUI timer cannot run in synchronous waitForFinished.
        QTRY_VERIFY_WITH_TIMEOUT(scheduledCallback, 500);
        qInfo() << "Offline fake PDF process blocked GUI timers for" << blockedMs << "ms; guarded launch prevented";
    }
    void autosaveDefersAutomaticallyAndPersistsLatestGeneration() {
        QTemporaryDir dir;
        Document d;
        Recovery recovery(&d, dir.path());
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("physical lease") : QString(); });
        prepare(d);
        QTRY_VERIFY_WITH_TIMEOUT(recovery.deferred(), 2200);
        QVERIFY(!QFile::exists(recovery.location()));
        d.setMetadata("latest generation", "not the first dirty state");
        d.setProperty("x", 63.);
        active = false;
        const auto expected = d.projectBytes();
        d.notifySynchronousWorkGuardChanged();
        QTRY_COMPARE_WITH_TIMEOUT(read(recovery.location()), expected, 1500);
        QVERIFY(!recovery.deferred());
        QVERIFY(d.dirty());
    }
    void fallbackRetryIsNotStarvedByGenericChangedSignals() {
        QTemporaryDir dir;
        Document d;
        Recovery recovery(&d, dir.path());
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        prepare(d);
        QVERIFY(!recovery.save());
        QTimer genericChanged;
        genericChanged.setInterval(30);
        connect(&genericChanged, &QTimer::timeout, &d, &Document::changed);
        genericChanged.start();
        active = false; // Intentionally omit guard notification: exercise fallback.
        const auto expected = d.projectBytes();
        QTRY_COMPARE_WITH_TIMEOUT(read(recovery.location()), expected, 1000);
        QVERIFY(!recovery.deferred());
    }
    void deferredDocumentReplacementDoesNotResurrectOldSnapshot() {
        QTemporaryDir dir;
        Document d;
        Recovery recovery(&d, dir.path());
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        prepare(d);
        QVERIFY(!recovery.save());
        QVERIFY(recovery.deferred());
        d.newProject();
        d.addShape("ellipse");
        d.setMetadata("replacement", "");
        active = false;
        const auto expected = d.projectBytes();
        d.notifySynchronousWorkGuardChanged();
        QTRY_COMPARE_WITH_TIMEOUT(read(recovery.location()), expected, 1200);
        QVERIFY(read(recovery.location()).contains("replacement"));
    }
    void closeAndDocumentDestructionCancelDeferredWork() {
        QTemporaryDir dir;
        auto d = std::make_unique<Document>();
        auto recovery = std::make_unique<Recovery>(d.get(), dir.path());
        bool active = true;
        d->addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        prepare(*d);
        QVERIFY(!recovery->save());
        recovery->closeSession();
        active = false;
        d->notifySynchronousWorkGuardChanged();
        QTest::qWait(350);
        QVERIFY(!QFile::exists(recovery->location()));
        recovery.reset();
        Recovery next(d.get(), dir.path());
        active = true;
        QVERIFY(!next.save());
        d.reset();
        active = false;
        QTest::qWait(350);
        QVERIFY(!next.save());
        QVERIFY(!QFile::exists(next.location()));
    }
    void cleanReplacementAndRecoveryDestructionCannotReviveSave() {
        QTemporaryDir dir;
        Document d;
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        {
            Recovery recovery(&d, dir.path());
            prepare(d);
            QVERIFY(!recovery.save());
            d.newProject();
            active = false;
            d.notifySynchronousWorkGuardChanged();
            QTRY_VERIFY_WITH_TIMEOUT(!recovery.deferred(), 800);
            QVERIFY(!QFile::exists(recovery.location()));
            active = true;
            d.addShape("rect");
            QVERIFY(!recovery.save());
        } // A live deferred timer is destroyed without closeSession.
        active = false;
        d.notifySynchronousWorkGuardChanged();
        QTest::qWait(350);
        QVERIFY(!QFile::exists(dir.filePath("autosave.blazer")));
    }
    void artControllerUsesSharedLeaseGuardBeforeConversion() {
        QTemporaryDir dir;
        Document d;
        prepare(d);
        Machine simulator(&d, nullptr, Machine::HardwareAccess::Disabled);
        ArtLibrary library(dir.filePath("art.json"));
        ArtLibraryController controller(&d, &simulator, &library);
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("shared physical lease") : QString(); });
        QCOMPARE(controller.blockReason(), QString("shared physical lease"));
        QVERIFY(!controller.reviewSelection().value("valid").toBool());
        QVERIFY(controller.thumbnailSource("missing").isEmpty());
        QVERIFY(library.entries().isEmpty());
        active = false;
        d.notifySynchronousWorkGuardChanged();
        QVERIFY(controller.reviewSelection().value("valid").toBool());
    }
    void pendingDecisionCleanupAlsoWaitsForIdle() {
        QTemporaryDir dir;
        const auto path = dir.filePath("autosave.blazer");
        write(path, "old recovery fixture");
        Document d;
        Recovery recovery(&d, dir.path());
        QVERIFY(recovery.pendingDecision());
        bool active = true;
        d.addSynchronousWorkGuard([&] { return active ? QString("active") : QString(); });
        recovery.clear();
        QVERIFY(!recovery.pendingDecision());
        QVERIFY(recovery.deferred());
        QCOMPARE(read(path), QByteArray("old recovery fixture"));
        active = false;
        d.notifySynchronousWorkGuardChanged();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 1000);
        QVERIFY(!recovery.deferred());
    }
};
QTEST_MAIN(SynchronousWorkTest)
#include "synchronouswork_test.moc"
