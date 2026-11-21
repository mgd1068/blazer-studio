#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QVariantMap>
#include <atomic>
#include <memory>

enum class ControllerNativeAction { FocusHold, StartJob, Jog, Home, Frame, WorkZero, Resume, Override };
// Revocation can remove input authority only. This token grants no motion or flow.
struct NativeInputContextToken { std::atomic_bool valid{true}; };
namespace ControllerNativeDigest {
inline QByteArray request(ControllerNativeAction action,double x=0,double y=0,
                          const QVariantMap &bounds={},const QByteArray &source={}) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream<<qint32(action)<<x<<y<<QJsonDocument::fromVariant(bounds).toJson(QJsonDocument::Compact)<<source;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
}
