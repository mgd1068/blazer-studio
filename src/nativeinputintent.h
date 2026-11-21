#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QPointF>
#include <optional>

// GUI-only provenance collector. Data returned here grants no machine authority.
// The worker still checks original deadline, connection and motion/output evidence.
class NativeInputIntentAdapter final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    struct Intent {
        quint64 interaction = 0;
        qint64 observedAtMs = -1, expiresAtMs = -1;
    };
    explicit NativeInputIntentAdapter(QObject *parent = nullptr);
    ~NativeInputIntentAdapter() override;
    void registerControl(QObject *control, const QString &action);
    std::optional<Intent> consume(const QString &action);
    void revoke();
    QString reason() const;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void invalidated(); // Release/context loss: end an active hold, never start one.
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private:
    friend class NativeInputIntentTestAccess;
    enum class Domain { Unknown, X11Server32, KWinMonotonic32 };
    struct Sample { quint32 server = 0; qint64 before = -1, after = -1; };
    struct Pending {
        Intent intent;
        QPointer<QObject> control, window;
        quint64 context = 0;
    };
    Domain m_domain = Domain::Unknown;
    Sample m_sample;
    QHash<QString, QPointer<QObject>> m_controls;
    QHash<QString, Pending> m_pending;
    quint64 m_context = 1, m_interaction = 0;
    qint64 m_contextStarted = 0, m_suspendGap = -1;
    void *m_xcb = nullptr;
    quint32 m_window = 0;
    qint64 m_probeBefore = -1;
    quint32 m_probeSequence = 0;
    QTimer m_probeTimer;
    quint64 m_platformEpoch = 1;
    bool m_checkPending = false;
    QString m_compositorOwner;
    void checkWaylandDomain();
    static qint64 nowMs();
    void probe();
    void sample(quint32 server, qint64 before, qint64 after);
    std::optional<Intent> mapTimestamp(quint64 timestamp, qint64 now);
    void capture(QObject *window, quint64 timestamp, const QPointF &scenePosition,
                 bool key, bool spontaneous, bool repeat, bool synthetic);
};
