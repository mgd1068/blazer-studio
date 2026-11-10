#pragma once
#include <QImage>
#include <QVariantMap>
#include <memory>

struct ReliefSettings {
    int passes = 16;
    double interval = .1, gamma = 1, depthPerPass = 0;
    bool inverted = false;
    bool operator==(const ReliefSettings &) const = default;
    QVariantMap toMap() const;
    static bool fromMap(const QVariantMap &map, ReliefSettings &result);
    // White is untouched, black receives all passes. Transparency is never engraved.
    int count(QRgb pixel) const;
};
struct ReliefAsset {
    QImage image;
    QByteArray png;
    static std::shared_ptr<const ReliefAsset> fromImage(const QImage &image);
    static std::shared_ptr<const ReliefAsset> parse(const QByteArray &png);
};

struct ReliefRaster {
    int columns = 0, rows = 0;
    QVector<int> counts; // world rows from bottom to top
    QString error;
    static ReliefRaster build(const ReliefAsset &asset, const ReliefSettings &settings,
                              const QSizeF &size);
};
