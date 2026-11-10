#pragma once
#include "job.h"
#include <QFutureWatcher>
#include <QThreadPool>
#include <QUrl>
struct BatchResult {
    QString error;
    int count = 0;
    bool cancelled = false;
};
class BatchExport : public QObject {
    Q_OBJECT
  public:
    explicit BatchExport(QObject *parent = nullptr);
    ~BatchExport() override;
    bool start(JobInput input, QUrl csv, QUrl directory, QString pattern);
    void cancel();
    bool busy() const { return m_active; }
    static BatchResult run(JobInput input, const QUrl &csv, const QUrl &directory,
                           const QString &pattern, const Cancellation &cancel = {});
  signals:
    void changed();
    void finished(QString message);

  private:
    QThreadPool m_pool;
    QFutureWatcher<BatchResult> m_watcher;
    Cancellation m_cancel;
    bool m_active = false;
};
