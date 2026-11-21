#include "controllerruntime.h"
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

class RuntimeLifecycleTest final : public QObject {
    Q_OBJECT
  private slots:
    void directOpenResultReceiverMayDeleteStandaloneFacade() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // This private directory remains owned for the whole request. No endpoint is
        // created: neither enumeration nor any real serial/hardware access is needed.
        const auto endpoint = directory.filePath("never-created-controller");
        QVERIFY(!QFileInfo::exists(endpoint));
        QPointer<ControllerRuntimeSession> runtime =
            new ControllerRuntimeSession(DeviceProfile::m3Reference());
        int results = 0;
        quint64 completedId = 0;
        bool opened = true;
        QString reason;
        QObject::connect(runtime, &ControllerSession::connectionRequestFinished, this,
                         [&](quint64 id, bool didOpen, const QString &failure) {
                             ++results;
                             completedId = id;
                             opened = didOpen;
                             reason = failure;
                             delete runtime.data(); // Direct receiver; deliberately not deleteLater().
                         }, Qt::DirectConnection);
        const auto request = runtime->requestOpen(endpoint, "lifecycle-owned-missing-endpoint");
        QVERIFY(request.enqueued);
        QVERIFY(request.requestId != 0);
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isNull(), 5000);
        QCOMPARE(results, 1);
        QCOMPARE(completedId, request.requestId);
        QVERIFY(!opened);
        QVERIFY(!reason.isEmpty());
        // Process queued worker/retirement notifications after destruction too.
        QTest::qWait(100);
        QCOMPARE(results, 1);
        QVERIFY(!QFileInfo::exists(endpoint));
    }

    void directWorkerChangedReceiverMayDeleteStandaloneFacade() {
        QPointer<ControllerRuntimeSession> runtime =
            new ControllerRuntimeSession(DeviceProfile::m3Reference());
        int changes = 0;
        QObject::connect(runtime, &ControllerSession::changed, this, [&] {
            ++changes;
            delete runtime.data();
        }, Qt::DirectConnection);
        // Initial worker snapshot arrives through the production queued notification
        // path. No test publication hook, request wrapper, or serial endpoint is used.
        QTRY_VERIFY_WITH_TIMEOUT(runtime.isNull(), 5000);
        QCOMPARE(changes, 1);
        QTest::qWait(100);
        QCOMPARE(changes, 1);
    }
};
QTEST_GUILESS_MAIN(RuntimeLifecycleTest)
#include "runtimelifecycle_test.moc"
