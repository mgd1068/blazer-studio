#pragma once
#include "document.h"
#include <QTimer>
class Recovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString location READ location CONSTANT)
  public:
    explicit Recovery(Document *document, const QString &directory = {}, QObject *parent = nullptr);
    bool available() const;
    QString location() const { return path; }
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool restore();
    Q_INVOKABLE void clear();
  signals:
    void changed();

  private:
    Document *document;
    QString path;
    QTimer timer;
    bool written = false;
};
