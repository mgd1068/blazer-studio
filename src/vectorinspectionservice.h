#pragma once
#include "vectorinspection.h"
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <functional>
class Document;
class VectorInspectionService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap report READ report NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
  public:
    using Inspector = std::function<VectorInspection::Report(const JobInput &, const QStringList &,
                                                             const Cancellation &)>;
    explicit VectorInspectionService(Document *document, QObject *parent = nullptr,
                                     Inspector inspector = {});
    ~VectorInspectionService() override;
    bool busy() const { return active; }
    QVariantMap report() const { return published; }
    QString message() const { return lastMessage; }
    Q_INVOKABLE bool inspect(bool selectionOnly = false);
    Q_INVOKABLE void cancel();
  signals:
    void changed();

  private:
    QStringList selectionIds() const;
    void invalidate();
    QPointer<Document> document;
    QThreadPool pool;
    QFutureWatcher<VectorInspection::Report> watcher;
    Cancellation cancellation;
    Inspector inspector;
    QVariantMap published;
    QString lastMessage;
    QByteArray sourceHash;
    std::optional<JobInput> sourceSnapshot;
    QStringList sourceSelection;
    quint64 sourceRevision = 0;
    bool active = false, selectedOnly = false, stale = false;
};
