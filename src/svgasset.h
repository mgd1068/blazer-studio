#pragma once
#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <memory>
class QSvgRenderer;

// Validated immutable SVG data is shared by history entries and parsed only once.
struct SvgAsset {
    QByteArray bytes;
    QSizeF sizeMm;
    std::shared_ptr<QSvgRenderer> renderer;
    static std::shared_ptr<const SvgAsset> parse(const QByteArray &bytes, QString &error);
};
