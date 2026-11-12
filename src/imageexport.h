#pragma once
#include "relief.h"
#include <QFutureWatcher>
#include <QThreadPool>
#include <QUrl>

struct ImageExportInput {
    std::shared_ptr<const ReliefAsset> asset;
    QVariantMap style;
    QSizeF sizeMm;
    bool processed = true;
};
struct ImageExportResult {
    QString error;
    QSize pixels{};
    bool cancelled = false;
};
class ImageExport : public QObject {
    Q_OBJECT
  public:
    explicit ImageExport(QObject *parent = nullptr);
    ~ImageExport() override;
    bool start(ImageExportInput input, const QUrl &target, QString &error);
    bool busy() const { return m_active; }
    void cancel();
    static ImageExportResult run(const ImageExportInput &input, const QUrl &target,
                                 const Cancellation &cancel = {});
  signals:
    void changed();
    void finished(QString message);

  private:
    QThreadPool m_pool;
    QFutureWatcher<ImageExportResult> m_watcher;
    Cancellation m_cancel;
    bool m_active = false;
};
