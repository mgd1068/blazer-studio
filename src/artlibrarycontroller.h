#pragma once
#include "artwork.h"
#include <QCache>
#include <QObject>
#include <QPointer>
class Document;
class Machine;
class ArtLibrary;
class ArtLibraryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap review READ review NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString blockReason READ blockReason NOTIFY changed)
  public:
    explicit ArtLibraryController(Document *document, Machine *machine, ArtLibrary *library,
                                  QObject *parent = nullptr);
    QVariantMap review() const { return currentReview; }
    QString message() const { return lastMessage; }
    QString blockReason() const;
    Q_INVOKABLE QVariantMap reviewSelection();
    Q_INVOKABLE QString saveSelection(QString name, QStringList tags, const QString &expectedToken,
                                      bool acceptConversion = false);
    Q_INVOKABLE void clearReview();
    Q_INVOKABLE bool insert(const QString &id, double xMm, double yMm);
    Q_INVOKABLE bool updateMetadata(const QString &id, QString name, QStringList tags);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QString thumbnailSource(const QString &id);
  signals:
    void changed();

  private:
    QList<Artwork> selection(QString &error) const;
    bool sourceMatches() const;
    bool fail(QString message);
    void invalidateSource();
    QPointer<Document> document;
    QPointer<Machine> machine;
    QPointer<ArtLibrary> library;
    QByteArray frozenGeometry;
    QList<Artwork> sourceItems;
    int sourcePage = -1;
    QString token, lastMessage;
    QVariantMap currentReview;
    QCache<QString, QString> thumbnails{16};
    bool mutating = false, signalingFailure = false, renderingThumbnail = false;
    struct Mutation {
        ArtLibraryController &owner;
        explicit Mutation(ArtLibraryController &value) : owner(value) { owner.mutating = true; }
        ~Mutation() {
            owner.mutating = false;
            emit owner.changed();
        }
    };
};
