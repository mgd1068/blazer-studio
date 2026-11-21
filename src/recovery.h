#pragma once
#include "document.h"
#include <QLockFile>
#include <QTimer>
#include <QPointer>
#include <memory>
class Recovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool deferred READ deferred NOTIFY changed)
    Q_PROPERTY(bool writable READ writable CONSTANT)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool pendingDecision READ pendingDecision NOTIFY changed)
    Q_PROPERTY(QString location READ location CONSTANT)
  public:
    explicit Recovery(Document *document, const QString &directory = {}, QObject *parent = nullptr);
    bool available() const;
    bool deferred() const { return workDeferred; }
    bool pendingDecision() const { return decisionPending; }
    bool writable() const { return lock && lock->isLocked(); }
    QString location() const { return path; }
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool restore();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void closeSession();
  signals:
    void changed();

  private:
    std::unique_ptr<QLockFile> lock;
    QPointer<Document> document;
    bool deferWhileBlocked();
    void retryDeferred();
    void setDeferred(bool value);
    QString path;
    QTimer timer;
    bool workDeferred = false, clearPending = false, recoveryExists = false;
    bool written = false, preservePrevious = false, restored = false;
    bool decisionPending = false, sessionClosed = false;
};
