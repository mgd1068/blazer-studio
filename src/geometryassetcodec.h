#pragma once
#include "artwork.h"
#include "cancellation.h"
#include <QByteArray>
#include <QVariantList>
namespace GeometryAssetCodec {
inline constexpr qsizetype maximumBytes = 16 * 1024 * 1024;
struct Review {
    QString error;
    QVariantList issues;
    bool conversionRequired = false;
    bool valid() const { return error.isEmpty(); }
};
struct Encoded {
    QByteArray bytes, contentHash;
    Review review;
    QString error;
    bool valid() const { return error.isEmpty() && !bytes.isEmpty(); }
};
struct Decoded {
    QList<Artwork> items;    // New UUIDs; all are non-output templates.
    QStringList sourceRoles; // Parallel: graphic/T1/T2, for explicit later assignment.
    QRectF boundsMm;         // Safe object-frame union normalized to lower-left (0,0).
    QByteArray canonicalBytes, contentHash;
    qint64 pixelCount = 0;
    int elementCount = 0;
    QString error;
    bool valid() const { return error.isEmpty() && !items.isEmpty(); }
};
Review review(const QList<Artwork> &visibleSelection);
Encoded encode(const QList<Artwork> &visibleSelection, bool acceptAppearanceConversion = false,
               const Cancellation &cancel = {});
Decoded decode(const QByteArray &bytes, const Cancellation &cancel = {});
QByteArray thumbnail(const QByteArray &bytes, QString &error, const Cancellation &cancel = {});
} // namespace GeometryAssetCodec
