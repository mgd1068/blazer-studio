#include "nativeinputintent.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLibrary>
#include <QFile>
#include <QFileInfo>
#include <QMetaProperty>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <chrono>
#include <limits>
#ifdef BLAZER_HAS_XCB_INPUT_CLOCK
#include <xcb/xcb.h>
#endif

qint64 NativeInputIntentAdapter::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
NativeInputIntentAdapter::NativeInputIntentAdapter(QObject *parent, bool enablePlatformInput) : QObject(parent) {
    m_contextStarted=nowMs();
    struct timespec boot{},mono{};
    if (clock_gettime(CLOCK_BOOTTIME,&boot)==0 && clock_gettime(CLOCK_MONOTONIC,&mono)==0)
        m_suspendGap=boot.tv_sec*1000LL+boot.tv_nsec/1000000-
                     (mono.tv_sec*1000LL+mono.tv_nsec/1000000);
    if (!enablePlatformInput || !qGuiApp) return;
    qGuiApp->installEventFilter(this);
    qGuiApp->installNativeEventFilter(this);
#if defined(BLAZER_HAS_XCB_INPUT_CLOCK) && QT_CONFIG(xcb)
    if (QGuiApplication::platformName() == "xcb") {
        if (auto native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            auto connection = native->connection(); // Same actual Qt server, never DISPLAY guess.
            if (connection && !xcb_connection_has_error(connection)) {
                const auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
                if (screen) {
                    m_xcb = connection;
                    m_window = xcb_generate_id(connection);
                    const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
                    xcb_create_window(connection, XCB_COPY_FROM_PARENT, m_window, screen->root,
                        0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
                        XCB_CW_EVENT_MASK, &mask);
                    m_domain = Domain::X11Server32;
                }
            }
        }
    }
#endif
    connect(&m_probeTimer, &QTimer::timeout, this, &NativeInputIntentAdapter::probe);
    m_probeTimer.setInterval(200);
    m_probeTimer.start();
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
        [this](Qt::ApplicationState state) { if (state != Qt::ApplicationActive) revoke(); });
    auto ownerWatcher = new QDBusServiceWatcher("org.kde.KWin",QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(ownerWatcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this] {
        if (m_domain == Domain::KWinMonotonic32) m_domain = Domain::Unknown;
        ++m_platformEpoch; m_checkPending=false; m_compositorOwner.clear();
        const QPointer<NativeInputIntentAdapter> alive(this);
        revoke(); if (!alive) return;
        checkWaylandDomain();
    });
    checkWaylandDomain();
    probe();
}
NativeInputIntentAdapter::~NativeInputIntentAdapter() {
    if (qGuiApp) qGuiApp->removeEventFilter(this);
#ifdef BLAZER_HAS_XCB_INPUT_CLOCK
    if (m_xcb && m_window) {
        auto c = static_cast<xcb_connection_t *>(m_xcb);
        xcb_destroy_window(c, m_window);
        xcb_flush(c);
    }
#endif
}
void NativeInputIntentAdapter::probe() {
    if (QGuiApplication::platformName().startsWith("wayland")) {
        if (m_domain==Domain::KWinMonotonic32 && !compositorPeerStillMatches()) {
            m_domain=Domain::Unknown; m_compositorOwner.clear(); ++m_platformEpoch;
            const QPointer<NativeInputIntentAdapter> alive(this);
            revoke(); if (!alive) return;
        }
        checkWaylandDomain();
        return;
    }
#ifdef BLAZER_HAS_XCB_INPUT_CLOCK
    if (m_domain != Domain::X11Server32 || !m_xcb) return;
    const auto now = nowMs();
    if (m_probeBefore >= 0 && now - m_probeBefore <= 100) return;
    m_probeBefore = now;
    const uint32_t value = ++m_probeSequence;
    auto c = static_cast<xcb_connection_t *>(m_xcb);
    const auto cookie = xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_window,
        XCB_ATOM_WM_NAME, XCB_ATOM_CARDINAL, 32, 1, &value);
    m_probeSequence = cookie.sequence;
    xcb_flush(c); // No wait, roundtrip or nested event loop.
#endif
}
bool NativeInputIntentAdapter::matchesCompositorPeer(qint64 peerPid,qint64 ownerPid,uint uid) {
    if (peerPid<=0 || ownerPid<=0) return false;
    if (peerPid==ownerPid) return true; // Already same-UID SO_PEERCRED, unique DBus owner.
    const auto peerPath=QString("/proc/%1/exe").arg(peerPid);
    if (QFileInfo(peerPath).symLinkTarget()!="/usr/bin/kwin_wayland_wrapper") return false;
    struct stat info{};
    if (::stat("/usr/bin/kwin_wayland_wrapper",&info)!=0 || info.st_uid!=0 ||
        (info.st_mode&(S_IWGRP|S_IWOTH))!=0 || !S_ISREG(info.st_mode)) return false;
    QFile status(QString("/proc/%1/status").arg(ownerPid));
    if (!status.open(QIODevice::ReadOnly)) return false;
    const auto data=status.read(8193);
    if (data.size()>8192) return false;
    bool name=false,parent=false,ids=false;
    for (const auto &line:data.split('\n')) {
        const auto fields=line.simplified().split(' ');
        if (fields.size()==2 && fields[0]=="Name:") name=fields[1]=="kwin_wayland";
        if (fields.size()==2 && fields[0]=="PPid:") {
            bool ok=false; const auto pid=fields[1].toLongLong(&ok); parent=ok && pid==peerPid;
        }
        if (fields.size()==5 && fields[0]=="Uid:") {
            ids=true;
            for(int i=1;i<5;++i) { bool ok=false; const auto value=fields[i].toUInt(&ok); ids &= ok && value==uid; }
        }
    }
    return name && parent && ids;
}
bool NativeInputIntentAdapter::compositorPeerStillMatches() const {
#if QT_CONFIG(wayland)
    auto native=qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    static QLibrary client("libwayland-client.so.0");
    using GetFd=int(*)(wl_display *);
    const auto getFd=reinterpret_cast<GetFd>(client.resolve("wl_display_get_fd"));
    if (!native || !native->display() || !getFd) return false;
    struct ucred credentials{}; socklen_t size=sizeof(credentials);
    return getsockopt(getFd(native->display()),SOL_SOCKET,SO_PEERCRED,&credentials,&size)==0 &&
        size==sizeof(credentials) && credentials.uid==getuid() && credentials.pid==m_peerPid &&
        matchesCompositorPeer(m_peerPid,m_ownerPid,getuid());
#else
    return false;
#endif
}
void NativeInputIntentAdapter::checkWaylandDomain() {
#if QT_CONFIG(wayland)
    if (!qGuiApp || !QGuiApplication::platformName().startsWith("wayland") || m_checkPending ||
        !m_compositorOwner.isEmpty()) return;
    auto native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native || !native->display()) return;
    // Resolve the public Wayland client function without assuming development
    // headers or touching Qt-private connection objects from another thread.
    static QLibrary client("libwayland-client.so.0");
    using GetFd=int(*)(wl_display *);
    const auto getFd=reinterpret_cast<GetFd>(client.resolve("wl_display_get_fd"));
    if (!getFd) return;
    struct ucred credentials{}; socklen_t size=sizeof(credentials);
    if (getsockopt(getFd(native->display()),SOL_SOCKET,SO_PEERCRED,&credentials,&size)!=0 ||
        size!=sizeof(credentials) || credentials.uid!=getuid() || credentials.pid<=0) return;
    const auto epoch=m_platformEpoch;
    m_checkPending=true;
    const auto bus=QDBusConnection::sessionBus();
    auto ownerCall=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","GetNameOwner");
    ownerCall << QString("org.kde.KWin");
    auto ownerWait=new QDBusPendingCallWatcher(bus.asyncCall(ownerCall,500),this);
    connect(ownerWait,&QDBusPendingCallWatcher::finished,this,
        [this,epoch,credentials,bus](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QString> ownerReply=*watcher; watcher->deleteLater();
        if (epoch!=m_platformEpoch) return;
        if (ownerReply.isError() || ownerReply.value().isEmpty()) { m_checkPending=false; return; }
        const auto owner=ownerReply.value();
        auto pidCall=QDBusMessage::createMethodCall("org.freedesktop.DBus","/org/freedesktop/DBus",
            "org.freedesktop.DBus","GetConnectionUnixProcessID");
        pidCall << owner;
        auto pidWait=new QDBusPendingCallWatcher(bus.asyncCall(pidCall,500),this);
        connect(pidWait,&QDBusPendingCallWatcher::finished,this,
            [this,epoch,credentials,bus,owner](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<uint> pidReply=*watcher; watcher->deleteLater();
            if (epoch!=m_platformEpoch) return;
            if (pidReply.isError() || !matchesCompositorPeer(credentials.pid,pidReply.value(),getuid())) { m_checkPending=false; return; }
            const auto ownerPid=pidReply.value();
            auto infoCall=QDBusMessage::createMethodCall(owner,"/KWin","org.kde.KWin","supportInformation");
            auto infoWait=new QDBusPendingCallWatcher(bus.asyncCall(infoCall,500),this);
            connect(infoWait,&QDBusPendingCallWatcher::finished,this,
                [this,epoch,owner,ownerPid,credentials](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<QString> reply=*watcher; watcher->deleteLater();
                if (epoch!=m_platformEpoch) return;
                m_checkPending=false;
                if (reply.isError()) return;
                const auto lines=reply.value().split('\n');
                if (QString::fromLatin1(qVersion())!="6.10.2" ||
                    !lines.contains("KWin version: 6.6.6") || !lines.contains("Qt Version: 6.10.2") ||
                    !lines.contains("Operation Mode: Wayland") || !lines.contains("Name: DRM")) return;
                // Trust the active local session compositor, not installed packages,
                // environment variables, proc visibility or a binary-attestation claim.
                m_compositorOwner=owner;
                m_peerPid=credentials.pid; m_ownerPid=ownerPid;
                if (!compositorPeerStillMatches()) { m_compositorOwner.clear(); return; }
                m_domain=Domain::KWinMonotonic32;
                revoke();
            });
        });
    });
#endif
}
bool NativeInputIntentAdapter::nativeEventFilter(const QByteArray &type, void *message, qintptr *) {
#ifdef BLAZER_HAS_XCB_INPUT_CLOCK
    if (m_domain == Domain::X11Server32 && type == "xcb_generic_event_t") {
        auto event = static_cast<xcb_generic_event_t *>(message);
        if (event->response_type == XCB_PROPERTY_NOTIFY) {
            auto property = reinterpret_cast<xcb_property_notify_event_t *>(event);
            if (property->window == m_window && property->atom == XCB_ATOM_WM_NAME &&
                event->full_sequence == m_probeSequence && m_probeBefore >= 0) {
                const QPointer<NativeInputIntentAdapter> alive(this);
                sample(property->time, m_probeBefore, nowMs());
                if (!alive) return false;
                m_probeBefore = -1;
            }
        }
    }
#else
    Q_UNUSED(type); Q_UNUSED(message);
#endif
    return false;
}
void NativeInputIntentAdapter::sample(quint32 server, qint64 before, qint64 after) {
    if (server == 0 || before < 0 || after < before || after-before > 30) {
        m_sample = {};
        revoke();
        return;
    }
    // A new sample must agree with the prior short-lived mapping. Server reset,
    // clock jumps and ambiguous modulo intervals do not silently recalibrate intents.
    if (m_sample.after >= 0 && after-m_sample.after <= 1000) {
        const auto advance = static_cast<qint32>(server-m_sample.server);
        if (advance < before-m_sample.after-10 || advance > after-m_sample.before+10) {
            const QPointer<NativeInputIntentAdapter> alive(this);
            revoke();
            if (!alive) return;
        }
    }
    m_sample = {server,before,after};
}
bool NativeInputIntentAdapter::clockContinuity() {
    struct timespec boot{}, monotonic{};
    if (clock_gettime(CLOCK_BOOTTIME,&boot)!=0 || clock_gettime(CLOCK_MONOTONIC,&monotonic)!=0) return false;
    const auto gap=boot.tv_sec*1000LL+boot.tv_nsec/1000000-
                   (monotonic.tv_sec*1000LL+monotonic.tv_nsec/1000000);
    if (m_suspendGap>=0 && (gap-m_suspendGap>20 || gap<m_suspendGap-20)) {
        m_suspendGap=gap; m_sample={}; revoke(); return false;
    }
    m_suspendGap=gap;
    return true;
}
std::optional<NativeInputIntentAdapter::Intent>
NativeInputIntentAdapter::mapTimestamp(quint64 timestamp, qint64 now) {
    if (!clockContinuity()) return {};
    if (m_domain==Domain::KWinMonotonic32) {
        if (timestamp==0 || timestamp>std::numeric_limits<quint32>::max()) return {};
        struct timespec monotonic{};
        if (clock_gettime(CLOCK_MONOTONIC,&monotonic)!=0) return {};
        const qint64 nativeNow=monotonic.tv_sec*1000LL+monotonic.tv_nsec/1000000;
        const auto age=static_cast<qint32>(quint32(nativeNow)-quint32(timestamp));
        // Sample both clock domains around mapping. Quantization plus mapping
        // work is subtracted from remaining budget, never added to it.
        const auto after=nowMs();
        const auto upper=qint64(age)+2+(after-now);
        if (age<0 || after<now || upper>250 || after-upper<=m_contextStarted) return {};
        return Intent{++m_interaction,after-upper,after-upper+250,{}, {}};
    }
    if (m_domain != Domain::X11Server32 || timestamp == 0 ||
        timestamp > std::numeric_limits<quint32>::max() || m_sample.after < 0 ||
        now < m_sample.after || now-m_sample.after > 500) return {};
    const auto sampleMinusEvent = static_cast<qint32>(m_sample.server-quint32(timestamp));
    // Offset lies in [before-server,after-server]. Include timestamp truncation
    // and a conservative 5ms drift/jitter reserve. Never use callback arrival as event time.
    const qint64 upperAge = now-m_sample.before+sampleMinusEvent+7;
    const qint64 lowerAge = now-m_sample.after+sampleMinusEvent-7;
    if (upperAge < 0 || lowerAge < -7 || upperAge > 250 || now-upperAge<=m_contextStarted) return {};
    return Intent{++m_interaction,now-upperAge,now-upperAge+250,{}, {}};
}
void NativeInputIntentAdapter::registerControl(QObject *control, const QString &action,BindingProvider binding) {
    if (action.isEmpty()) return;
    if(!m_controls.contains(action)&&m_controls.size()>=32)return;
    if(binding)m_holdBindings[action]=std::move(binding);
    if (m_controls.value(action) == control) return;
    m_controls[action] = control; // Publish before focus-revocation receivers can re-register.
    const QPointer<NativeInputIntentAdapter> alive(this);
    invalidateAction(action,true);
    if (!alive || m_controls.value(action)!=control) return;
    const QPointer<QObject> registered(control);
    if (auto item = qobject_cast<QQuickItem *>(control)) {
        connect(item, &QQuickItem::windowChanged, this, [this,action,registered] { if(m_controls.value(action)==registered)invalidateAction(action,true); });
        connect(item, &QQuickItem::visibleChanged, this, [this,action,registered] { if(m_controls.value(action)==registered)invalidateAction(action,true); });
        // Disabling the button after admitted output must not revoke that frozen
        // request. Unconsumed press/release state is still discarded immediately.
        connect(item, &QQuickItem::enabledChanged, this, [this,action,registered] { if(m_controls.value(action)==registered)invalidateAction(action,false); });
        connect(item, &QQuickItem::activeFocusChanged, this, [this,item,action,registered] {
            if (item->hasActiveFocus() || m_controls.value(action)!=registered) return;
            for(const auto *entries:{&m_pending,&m_rawPress,&m_armed})
                if(entries->value(action).key){invalidateAction(action,true);return;}
        });
    }
    if (control) connect(control, &QObject::destroyed, this, [this,action,registered] {
        if(m_controls.value(action)!=registered)return;
        const QPointer<NativeInputIntentAdapter> alive(this);invalidateAction(action,true);
        if(!alive)return;
        if(m_controls.value(action).isNull()){m_controls.remove(action);m_clickBindings.remove(action);m_holdBindings.remove(action);m_tokens.remove(action);m_actionCutoff.remove(action);}
    });
}
void NativeInputIntentAdapter::registerClickControl(QObject *control,const QString &key,BindingProvider binding) {
    if(key=="focus"||!binding)return;
    const QPointer<NativeInputIntentAdapter> alive(this);
    registerControl(control,key);
    if(!alive||m_controls.value(key)!=control)return;
    m_clickBindings[key]=std::move(binding);
    invalidateAction(key,true);
    if (!control) return;
    const auto meta=control->metaObject();
    const auto pressed=meta->indexOfProperty("pressed");
    const auto eligibility=meta->indexOfProperty("nativeActionEnabled");
    if(pressed<0||eligibility<0||!meta->property(pressed).hasNotifySignal()||
       !meta->property(eligibility).hasNotifySignal()||meta->property(pressed).metaType().id()!=QMetaType::Bool||
       meta->property(eligibility).metaType().id()!=QMetaType::Bool){m_clickBindings[key]={};return;}
    for (const auto pair : {qMakePair(pressed,"controlPressedChanged()"),qMakePair(eligibility,"controlEligibilityChanged()")}) {
        if (pair.first<0 || !meta->property(pair.first).hasNotifySignal()) continue;
        const auto slot=metaObject()->indexOfSlot(pair.second);
        QObject::connect(control,meta->property(pair.first).notifySignal(),this,metaObject()->method(slot),Qt::UniqueConnection);
    }
}
void NativeInputIntentAdapter::invalidateAction(const QString &key,bool revokeSubmitted) {
    m_pending.remove(key);m_rawPress.remove(key);m_armed.remove(key);
    m_actionCutoff[key]=nowMs();
    if (revokeSubmitted) {
        if (auto token=m_tokens.take(key)) token->valid=false;
    }
    if (key=="focus") emit invalidated();
}
void NativeInputIntentAdapter::revokeFocus() { invalidateAction("focus",true); }
void NativeInputIntentAdapter::revoke() {
    ++m_context;
    m_contextStarted=nowMs();
    m_pending.clear();m_rawPress.clear();m_armed.clear();
    for(const auto &token:m_tokens)token->valid=false;
    m_tokens.clear();
    emit invalidated();
}
bool NativeInputIntentAdapter::eligible(QObject *control,bool click) {
    if(!control)return false;
    const auto property=control->property("nativeActionEnabled");
    return !property.isValid() ? !click : property.metaType().id()==QMetaType::Bool&&property.toBool();
}
bool NativeInputIntentAdapter::hitControl(QObject *windowObject,QObject *control,const QPointF &scene,bool key) {
    auto window=qobject_cast<QQuickWindow *>(windowObject);
    auto item=qobject_cast<QQuickItem *>(control);
    if(!window||!item||!window->isActive()||!window->isVisible()||!item->isVisible()||!item->isEnabled()||item->window()!=window)return false;
    if(key)return item->hasActiveFocus();
    auto hit=window->contentItem();
    for(int depth=0;hit&&depth<64;++depth){auto point=hit->mapFromScene(scene);auto child=hit->childAt(point.x(),point.y());if(!child)break;hit=child;if(depth==63)return false;}
    for(auto ancestor=hit;ancestor;ancestor=ancestor->parentItem())if(ancestor==item)return item->contains(item->mapFromScene(scene));
    return false;
}
QString NativeInputIntentAdapter::reason() const {
    if (m_domain == Domain::Unknown) return "Native Eingabe gesperrt: Zeitdomäne dieser Plattform ist ungeprüft.";
    if (m_domain == Domain::X11Server32 &&
        (m_sample.after < 0 || nowMs()-m_sample.after > 500))
        return "Native Eingabe gesperrt: frische X11-Zeitkalibrierung fehlt.";
    return {};
}
void NativeInputIntentAdapter::capture(QObject *windowObject, quint64 timestamp,
        const QPointF &scene, bool key, bool spontaneous, bool repeat, bool synthetic,int detail,const QInputDevice *device) {
    if (!spontaneous || repeat || synthetic) return;
    m_rawPress.clear();m_armed.clear(); // A second press cannot inherit an older interaction.
    if (m_domain==Domain::KWinMonotonic32 && !compositorPeerStillMatches()) {
        m_domain=Domain::Unknown; m_compositorOwner.clear(); ++m_platformEpoch;
        revoke(); return;
    }
    // KWin input-method forwarding can reuse a previous seat timestamp. Its
    // keyboard provenance is insufficient even when the compositor clock is known.
    if (key && m_domain==Domain::KWinMonotonic32) return;
    auto window = qobject_cast<QQuickWindow *>(windowObject);
    if (!window || !window->isActive() || !window->isVisible()) return;
    const QPointer<NativeInputIntentAdapter> alive(this);
    const auto mapped = mapTimestamp(timestamp,nowMs());
    if (!alive || !mapped) return;
    QString match;
    QByteArray binding;
    for (auto i=m_controls.cbegin(); i!=m_controls.cend(); ++i) {
        const bool click=m_clickBindings.contains(i.key());
        if (hitControl(window,i.value(),scene,key) && eligible(i.value(),click) &&
            mapped->observedAtMs>m_actionCutoff.value(i.key(),-1)) {
            const auto provider=click?m_clickBindings.value(i.key()):m_holdBindings.value(i.key());
            const auto current=provider ? provider() : QByteArray();
            if(!alive)return;
            if((click&&!provider)||(provider&&current.isEmpty()))continue;
            if (!match.isEmpty()) return; // Overlapping registrations are ambiguous.
            match=i.key();binding=current;
        }
    }
    if (!match.isEmpty()) {
        auto token=m_tokens.value(match);
        if(!token){token=std::make_shared<NativeInputContextToken>();m_tokens[match]=token;}
        auto intent=*mapped;intent.binding=binding;intent.contextToken=token;
        Pending pending{intent,m_controls.value(match),window,m_context,key,m_clickBindings.contains(match),detail,device};
        if(pending.click)m_rawPress[match]=pending;else m_pending[match]=pending;
    }
}
void NativeInputIntentAdapter::controlPressedChanged() {
    acceptControlPress(sender());
}
void NativeInputIntentAdapter::acceptControlPress(QObject *control) {
    if(!control)return;
    const auto pressed=control->property("pressed");if(pressed.metaType().id()!=QMetaType::Bool)return;
    for(const auto &key:m_clickBindings.keys())if(m_controls.value(key)==control) {
        if(!pressed.toBool()){m_rawPress.remove(key);m_armed.remove(key);continue;}
        const auto raw=m_rawPress.take(key);
        if(raw.control==control && raw.context==m_context && eligible(control,true) &&
            raw.intent.contextToken && raw.intent.contextToken->valid && nowMs()<=raw.intent.expiresAtMs &&
            m_clickBindings.value(key)()==raw.intent.binding) m_armed[key]=raw;
    }
}
void NativeInputIntentAdapter::controlEligibilityChanged() {
    auto control=sender();
    for(const auto &key:m_clickBindings.keys())if(m_controls.value(key)==control)invalidateAction(key,true);
}
void NativeInputIntentAdapter::release(QObject *window,quint64 timestamp,const QPointF &scene,
        bool key,bool spontaneous,bool repeat,bool synthetic,int detail,const QInputDevice *device) {
    if(!spontaneous||repeat||synthetic||(key&&m_domain==Domain::KWinMonotonic32))return;
    const QPointer<NativeInputIntentAdapter> alive(this);
    const auto mapped=mapTimestamp(timestamp,nowMs());if(!alive||!mapped)return;
    for(const auto &name:m_armed.keys()) {
        auto pending=m_armed.take(name);auto control=pending.control.data();
        const auto pressed=control?control->property("pressed"):QVariant();
        if(pending.window!=window||pending.key!=key||pending.detail!=detail||pending.device!=device||
            pending.context!=m_context||!hitControl(window,control,scene,key)||!eligible(control,true)||
            pressed.metaType().id()!=QMetaType::Bool||!pressed.toBool()||
            !pending.intent.contextToken||!pending.intent.contextToken->valid||
            nowMs()-pending.intent.observedAtMs>5000||
            m_clickBindings.value(name)()!=pending.intent.binding)continue;
        auto activation=*mapped;activation.binding=pending.intent.binding;activation.contextToken=pending.intent.contextToken;
        pending.intent=activation;m_pending[name]=pending;
    }
}
std::optional<NativeInputIntentAdapter::Intent> NativeInputIntentAdapter::consumeMatching(const QString &prefix,const QByteArray &binding) {
    QString match;
    bool candidate=false;
    for(auto i=m_pending.cbegin();i!=m_pending.cend();++i)if(i.key().startsWith(prefix+"#")) {
        candidate=true;if(i.value().intent.binding!=binding)continue;
        if(!match.isEmpty()){discardMatching(prefix);return{};}match=i.key();
    }
    if(match.isEmpty()&&candidate)discardMatching(prefix);
    return match.isEmpty()?std::nullopt:consume(match);
}
void NativeInputIntentAdapter::discardMatching(const QString &prefix) {
    for(const auto &key:m_controls.keys())if(key.startsWith(prefix+"#"))invalidateAction(key,false);
}
std::optional<NativeInputIntentAdapter::Intent>
NativeInputIntentAdapter::consume(const QString &action) {
    auto i=m_pending.find(action);
    if (i==m_pending.end()) return {};
    const auto pending=i.value();
    m_pending.erase(i); // One-shot, including denied/expired attempts.
    const QPointer<NativeInputIntentAdapter> alive(this);
    if (!clockContinuity() || !alive) return {};
    if (m_domain==Domain::KWinMonotonic32 && !compositorPeerStillMatches()) {
        m_domain=Domain::Unknown; m_compositorOwner.clear(); ++m_platformEpoch;
        revoke(); return {};
    }
    auto item=qobject_cast<QQuickItem *>(pending.control.data());
    auto window=qobject_cast<QQuickWindow *>(pending.window.data());
    const auto pressed=item ? item->property("pressed") : QVariant();
    if (pending.context!=m_context || !item || !window || !window->isActive() ||
        !window->isVisible() || !item->isEnabled() || !item->isVisible() ||
        pressed.metaType().id()!=QMetaType::Bool || pressed.toBool()==pending.click ||
        !eligible(item,pending.click) || !pending.intent.contextToken || !pending.intent.contextToken->valid ||
        (pending.click&&(!m_clickBindings.value(action)||m_clickBindings.value(action)()!=pending.intent.binding)) ||
        (!pending.click&&m_holdBindings.contains(action)&&m_holdBindings.value(action)()!=pending.intent.binding) ||
        (pending.key && !item->hasActiveFocus()) ||
        item->window()!=window || m_controls.value(action)!=item ||
        nowMs()>pending.intent.expiresAtMs || !reason().isEmpty()) return {};
    return pending.intent;
}
bool NativeInputIntentAdapter::eventFilter(QObject *object, QEvent *event) {
    if (!qobject_cast<QQuickWindow *>(object)) return false;
    switch(event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto mouse=static_cast<QMouseEvent *>(event);
        if (mouse->button()==Qt::LeftButton)
            capture(object,mouse->timestamp(),mouse->position(),false,event->spontaneous(),false,
                mouse->source()!=Qt::MouseEventNotSynthesized,int(mouse->button()),mouse->device());
        break;
    }
    case QEvent::KeyPress: {
        auto key=static_cast<QKeyEvent *>(event);
        if (key->key()==Qt::Key_Space || key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter)
            capture(object,key->timestamp(),{},true,event->spontaneous(),key->isAutoRepeat(),false,key->key(),key->device());
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto mouse=static_cast<QMouseEvent *>(event);const QPointer<NativeInputIntentAdapter> alive(this);
        release(object,mouse->timestamp(),mouse->position(),false,event->spontaneous(),false,
            mouse->source()!=Qt::MouseEventNotSynthesized,int(mouse->button()),mouse->device());
        if(alive)revokeFocus();
        break;
    }
    case QEvent::KeyRelease: {
        auto key=static_cast<QKeyEvent *>(event);const QPointer<NativeInputIntentAdapter> alive(this);
        release(object,key->timestamp(),{},true,event->spontaneous(),key->isAutoRepeat(),false,key->key(),key->device());
        if(alive)revokeFocus();
        break;
    }
    case QEvent::TouchCancel:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::Close:
        for(const auto &control:m_controls)if(auto item=qobject_cast<QQuickItem *>(control.data());item&&item->window()==object){revoke();break;}
        break;
    default: break;
    }
    return false;
}
