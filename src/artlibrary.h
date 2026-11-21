#pragma once
#include "geometryassetcodec.h"
#include <QCache>
#include <QObject>
#include <memory>
class QLockFile;
// Own geometry library only. Metadata access never returns embedded asset bytes.
class ArtLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool writable READ writable NOTIFY changed)
  public:
    explicit ArtLibrary(QString path = {}, QObject *parent = nullptr);
    ~ArtLibrary() override;
    QVariantList entries() const;
    QString message() const { return lastMessage; }
    bool writable() const { return !blocked; }
    Q_INVOKABLE QVariantList search(const QString &query) const;
    Q_INVOKABLE QString add(QString name, QStringList tags, const QByteArray &geometry);
    Q_INVOKABLE bool updateMetadata(const QString &id, QString name, QStringList tags);
    Q_INVOKABLE bool remove(const QString &id);
    QByteArray geometry(const QString &id) const;
    QByteArray thumbnail(const QString &id);
  signals:
    void changed();

  private:
    struct Entry {
        QString id, name, createdUtc, updatedUtc;
        QStringList tags;
        QByteArray geometry, hash;
        QSizeF size;
        int objects = 0, elements = 0;
        qint64 pixels = 0;
    };
    static QVariantMap metadata(const Entry &entry);
    bool fail(QString message, bool protect = false);
    bool persist(const QList<Entry> &next);
    static bool normalize(QString &name, QStringList &tags);
    int index(const QString &id) const;
    QString path, libraryId, lastMessage;
    QList<Entry> items;
    std::unique_ptr<QLockFile> lock;
    QCache<QString, QByteArray> previews{16};
    QByteArray diskHash;
    bool existed = false, blocked = false;
};
