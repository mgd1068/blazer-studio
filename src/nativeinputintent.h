#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QPointF>
#include "nativeaction.h"
#include <functional>
#include <optional>
class QInputDevice;

// GUI-only provenance collector. Data returned here grants no machine authority.
// The worker still checks original deadline, connection and motion/output evidence.
class NativeInputIntentAdapter final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    struct Intent {
        quint64 interaction = 0;
        qint64 observedAtMs = -1, expiresAtMs = -1;
        QByteArray binding;
        std::shared_ptr<const NativeInputContextToken> contextToken;
    };
    explicit NativeInputIntentAdapter(QObject *parent = nullptr, bool enablePlatformInput = true);
    ~NativeInputIntentAdapter() override;
    using BindingProvider = std::function<QByteArray()>;
    void registerControl(QObject *control, const QString &action,BindingProvider binding = {});
    void registerClickControl(QObject *control,const QString &key,BindingProvider binding);
    std::optional<Intent> consume(const QString &action);
    std::optional<Intent> consumeMatching(const QString &prefix,const QByteArray &binding);
    void discardMatching(const QString &prefix);
    void revokeFocus();
    void revoke();
    void revokeActionIntents(); // Retire inputs; committed typed reference keeps its window context.
    QString reason() const;
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void invalidated(); // Release/context loss: end an active hold, never start one.
    void contextInvalidated(); // Global window/device context, not ordinary focus release.
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private:
    friend class NativeInputIntentTestAccess;
    void revokeImpl(bool globalContext);
    enum class Domain { Unknown, X11Server32, KWinMonotonic32 };
    struct Sample { quint32 server = 0; qint64 before = -1, after = -1; };
    struct Pending {
        Intent intent;
        QPointer<QObject> control, window;
        quint64 context = 0;
        bool key = false;
        bool click = false;
        int detail = 0;
        const QInputDevice *device = nullptr;
    };
    Domain m_domain = Domain::Unknown;
    Sample m_sample;
    QHash<QString, QPointer<QObject>> m_controls;
    QHash<QString, Pending> m_pending;
    QHash<QString, Pending> m_rawPress, m_armed;
    QHash<QString,BindingProvider> m_clickBindings;
    QHash<QString,BindingProvider> m_holdBindings;
    QHash<QString,std::shared_ptr<NativeInputContextToken>> m_tokens;
    QHash<QString,qint64> m_actionCutoff;
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
    qint64 m_peerPid = -1, m_ownerPid = -1;
    static bool matchesCompositorPeer(qint64 peerPid, qint64 ownerPid, uint uid);
    bool compositorPeerStillMatches() const;
    void checkWaylandDomain();
    static qint64 nowMs();
    bool clockContinuity();
    void probe();
    void sample(quint32 server, qint64 before, qint64 after);
    std::optional<Intent> mapTimestamp(quint64 timestamp, qint64 now);
    void capture(QObject *window, quint64 timestamp, const QPointF &scenePosition,
                 bool key, bool spontaneous, bool repeat, bool synthetic,
                 int detail = 0,const QInputDevice *device = nullptr);
    void release(QObject *window,quint64 timestamp,const QPointF &scenePosition,
                 bool key,bool spontaneous,bool repeat,bool synthetic,int detail,
                 const QInputDevice *device);
    void invalidateAction(const QString &key,bool revokeSubmitted);
    void acceptControlPress(QObject *control);
    static bool eligible(QObject *control,bool click);
    static bool hitControl(QObject *window,QObject *control,const QPointF &scene,bool key);
  private slots:
    void controlPressedChanged();
    void controlEligibilityChanged();
};
