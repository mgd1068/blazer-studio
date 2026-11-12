#pragma once
#include "deviceprofile.h"
#include <QImage>
#include <QObject>
#include <QTransform>
#include <QUrl>
#include <QVariantList>
class Registration : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
    Q_PROPERTY(bool calibrated READ calibrated NOTIFY changed)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY changed)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY changed)
    Q_PROPERTY(QUrl source READ source NOTIFY changed)
  public:
    explicit Registration(QObject *parent = nullptr) : QObject(parent) {}
    bool visible() const { return shown; }
    void setVisible(bool value) {
        shown = value && valid;
        emit changed();
    }
    bool calibrated() const { return valid; }
    int imageWidth() const { return image.width(); }
    int imageHeight() const { return image.height(); }
    QUrl source() const { return url; }
    Q_INVOKABLE bool load(const QUrl &file);
    Q_INVOKABLE bool calibrate(const QVariantList &pixels, const QVariantList &world);
    void setDeviceProfile(const DeviceProfile &profile);
    const QImage &frame() const { return image; }
    QTransform transform() const { return mapping; }
  signals:
    void changed();

  private:
    QUrl url;
    QImage image;
    QTransform mapping;
    bool shown = false, valid = false;
    DeviceProfile m_deviceProfile = DeviceProfile::m3Reference();
};
