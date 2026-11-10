#pragma once
#include "document.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
class DepthService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
  public:
    DepthService(Document *document, const QString &script, QObject *parent = nullptr);
    bool busy() const { return process.state() != QProcess::NotRunning; }
    QString message() const { return status; }
    Q_INVOKABLE bool estimate(const QUrl &image);
    Q_INVOKABLE void cancel() { process.kill(); }
  signals:
    void changed();

  private:
    Document *document;
    QString script, status;
    QProcess process;
    QTemporaryDir temp;
    QTimer deadline;
};
