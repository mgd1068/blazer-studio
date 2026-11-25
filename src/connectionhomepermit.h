#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonDocument>
#include <QString>
#include <QVariantMap>
#include <memory>

namespace ControllerConnectionHomeDigest {
inline QByteArray context(const QString &endpoint,const QByteArray &binding,
                          const QByteArray &profileFingerprint) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream<<QByteArray("connect-and-home-context-v1")<<endpoint<<binding<<profileFingerprint;
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
inline QByteArray record(const QVariantMap &record) {
    return QCryptographicHash::hash(QByteArray("connect-and-home-calibration-v1:")+
        QJsonDocument::fromVariant(record).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
}
// A committed bounded connection operation, NOT an extended fresh-input proof.
// Minted only after Machine consumed the genuine 250-ms native connect click.
// No deserializer, public constructor, bool grant, or QML entry point.
class ConnectionHomePermit final {
    friend class ControllerRuntimeSession;
    friend class ControllerRuntimeWorker;
    friend class ControllerRuntimeTestAccess;
    ConnectionHomePermit(quint64 issuerValue,quint64 interactionValue,quint64 requestValue,
                         quint64 generationValue,quint64 epochValue,qint64 observedValue,
                         qint64 admittedValue,QString endpointValue,QByteArray bindingValue,
                         QByteArray profileValue,QByteArray recordValue,QByteArray contextValue)
        :issuer(issuerValue),interaction(interactionValue),requestId(requestValue),
         generation(generationValue),preOpenEpoch(epochValue),observedAt(observedValue),
         admittedAt(admittedValue),deadline(observedValue+15000),endpoint(std::move(endpointValue)),
         binding(std::move(bindingValue)),profileFingerprint(std::move(profileValue)),
         recordDigest(std::move(recordValue)),contextDigest(std::move(contextValue)) {}
    const quint64 issuer,interaction,requestId,generation,preOpenEpoch;
    const qint64 observedAt,admittedAt,deadline;
    const QString endpoint;
    const QByteArray binding,profileFingerprint,recordDigest,contextDigest;
};
