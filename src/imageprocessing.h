#pragma once
#include <QImage>
#include <QVariantMap>
namespace ImageProcessing {
QImage apply(const QImage &source, const QVariantMap &settings, QString &error);
}
