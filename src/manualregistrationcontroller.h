#pragma once
#include "manualregistration.h"
#include <QObject>
#include <QPointer>
class Document;
class Machine;
class ManualRegistrationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap review READ review NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString blockReason READ blockReason NOTIFY changed)
  public:
    ManualRegistrationController(Document *document, Machine *machine, QObject *parent=nullptr);
    QVariantMap review() const { return m_review; }
    QVariantMap state() const;
    QString message() const { return m_message; }
    QString blockReason() const;
    // Eight canonical-mm values: design1 XY, target1 XY, design2 XY, target2 XY.
    Q_INVOKABLE QVariantMap reviewPoints(const QVariantList &values, bool proportional,
                                        double maximumResidualMm);
    Q_INVOKABLE QVariantMap selectionReference() const;
    Q_INVOKABLE bool applyReview(const QString &expectedToken);
    Q_INVOKABLE void clearReview();
    Q_INVOKABLE bool resetAlignment();
  signals:
    void changed();
  private:
    bool fail(QString message);
    bool sourceStillCurrent() const;
    void sourceChanged();
    QPointer<Document> m_document;
    QPointer<Machine> m_machine;
    QVariantMap m_review;
    QString m_message, m_token;
    std::optional<ManualRegistration::Intent> m_intent;
    std::optional<JobPlacement::PositionSnapshot> m_position;
    QByteArray m_geometry, m_frame;
    QList<Artwork> m_sourceItems;
    QVariantList m_sourceLayers;
    QStringList m_sourceSelection;
    int m_sourcePage = -1;
    bool m_sourceCutSelected = false;
};
