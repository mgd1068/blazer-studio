#include "document.h"
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
bool Document::importModel(const QUrl &url) {
    if (!url.isLocalFile())
        return false;
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024)
        return false;
    QByteArray data = file.readAll();
    QList<std::array<QVector3D, 3>> triangles;
    auto suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
    if (suffix == "obj") {
        QList<QVector3D> vertices;
        for (auto line : QString::fromUtf8(data).split('\n')) {
            auto parts = line.simplified().split(' ');
            if (parts.size() >= 4 && parts[0] == "v")
                vertices.append({parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()});
            if (parts.size() >= 4 && parts[0] == "f") {
                QList<int> ids;
                for (int i = 1; i < parts.size(); ++i) {
                    bool ok;
                    int index = parts[i].section('/', 0, 0).toInt(&ok);
                    if (index < 0)
                        index = vertices.size() + index;
                    else
                        --index;
                    if (!ok || index < 0 || index >= vertices.size())
                        return false;
                    ids.append(index);
                }
                for (int i = 1; i + 1 < ids.size(); ++i)
                    triangles.append({vertices[ids[0]], vertices[ids[i]], vertices[ids[i + 1]]});
            }
            if (vertices.size() > 300000 || triangles.size() > 100000)
                return false;
        }
    } else if (suffix == "stl") {
        bool binary = false;
        if (data.size() >= 84) {
            QDataStream stream(data.mid(80, 4));
            stream.setByteOrder(QDataStream::LittleEndian);
            quint32 count;
            stream >> count;
            binary = 84 + qint64(count) * 50 == data.size();
            if (binary) {
                if (count > 100000)
                    return false;
                QDataStream in(data);
                in.setByteOrder(QDataStream::LittleEndian);
                in.setFloatingPointPrecision(QDataStream::SinglePrecision);
                in.skipRawData(84);
                for (quint32 i = 0; i < count; ++i) {
                    float ignored;
                    for (int n = 0; n < 3; ++n)
                        in >> ignored;
                    std::array<QVector3D, 3> tri;
                    for (auto &v : tri) {
                        float x, y, z;
                        in >> x >> y >> z;
                        v = {x, y, z};
                    }
                    quint16 attribute;
                    in >> attribute;
                    triangles.append(tri);
                }
                if (in.status() != QDataStream::Ok)
                    return false;
            }
        }
        if (!binary) {
            QRegularExpression re(
                "vertex\\s+([+\\-\\d.eE]+)\\s+([+\\-\\d.eE]+)\\s+([+\\-\\d.eE]+)");
            auto matches = re.globalMatch(QString::fromUtf8(data));
            QList<QVector3D> points;
            while (matches.hasNext()) {
                auto m = matches.next();
                bool x, y, z;
                QVector3D v(m.captured(1).toFloat(&x), m.captured(2).toFloat(&y),
                            m.captured(3).toFloat(&z));
                if (!x || !y || !z)
                    return false;
                points.append(v);
                if (points.size() == 3) {
                    triangles.append({points[0], points[1], points[2]});
                    points.clear();
                }
                if (triangles.size() > 100000)
                    return false;
            }
            if (!points.isEmpty())
                return false;
        }
    } else {
        notify("3D-Import unterstützt STL und OBJ.");
        return false;
    }
    if (triangles.isEmpty()) {
        notify("Modell enthält keine Dreiecke.");
        return false;
    }
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (auto tri : triangles)
        for (auto v : tri)
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(v[k]) || std::abs(v[k]) > 1e9)
                    return false;
                lo[k] = std::min(lo[k], v[k]);
                hi[k] = std::max(hi[k], v[k]);
            }
    auto extent = hi - lo;
    if (extent.x() <= 1e-6 || extent.y() <= 1e-6 || extent.z() <= 1e-6) {
        notify("Modell muss Ausdehnung in X/Y/Z besitzen.");
        return false;
    }
    int width = std::max(1, int(512 * extent.x() / std::max(extent.x(), extent.y()))),
        height = std::max(1, int(512 * extent.y() / std::max(extent.x(), extent.y())));
    QVector<float> zbuffer(width * height, -1e30f);
    qint64 checks = 0;
    for (auto tri : triangles) {
        for (auto &v : tri) {
            v.setX((v.x() - lo.x()) / extent.x() * width);
            v.setY((v.y() - lo.y()) / extent.y() * height);
        }
        int x0 = std::clamp(int(std::floor(std::min({tri[0].x(), tri[1].x(), tri[2].x()}))), 0,
                            width - 1),
            x1 = std::clamp(int(std::ceil(std::max({tri[0].x(), tri[1].x(), tri[2].x()}))), 0,
                            width - 1);
        int y0 = std::clamp(int(std::floor(std::min({tri[0].y(), tri[1].y(), tri[2].y()}))), 0,
                            height - 1),
            y1 = std::clamp(int(std::ceil(std::max({tri[0].y(), tri[1].y(), tri[2].y()}))), 0,
                            height - 1);
        double determinant = (tri[1].y() - tri[2].y()) * (tri[0].x() - tri[2].x()) +
                             (tri[2].x() - tri[1].x()) * (tri[0].y() - tri[2].y());
        if (std::abs(determinant) < 1e-12)
            continue;
        checks += qint64(x1 - x0 + 1) * (y1 - y0 + 1);
        if (checks > 50000000) {
            notify("3D-Modell für Tiefenraster zu aufwändig.");
            return false;
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double a = ((tri[1].y() - tri[2].y()) * (x + .5 - tri[2].x()) +
                            (tri[2].x() - tri[1].x()) * (y + .5 - tri[2].y())) /
                           determinant;
                double b = ((tri[2].y() - tri[0].y()) * (x + .5 - tri[2].x()) +
                            (tri[0].x() - tri[2].x()) * (y + .5 - tri[2].y())) /
                           determinant;
                double c = 1 - a - b;
                if (a >= -1e-9 && b >= -1e-9 && c >= -1e-9)
                    zbuffer[y * width + x] =
                        std::max(zbuffer[y * width + x],
                                 float(a * tri[0].z() + b * tri[1].z() + c * tri[2].z()));
            }
    }
    QImage image(width, height, QImage::Format_RGBA64);
    image.fill(Qt::transparent);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float z = zbuffer[y * width + x];
            if (z < -1e29)
                continue;
            quint16 gray = std::clamp(qRound((z - lo.z()) / extent.z() * 65535), 0, 65535);
            image.setPixelColor(x, height - 1 - y, QColor::fromRgba64(gray, gray, gray, 65535));
        }
    auto asset = ReliefAsset::fromImage(image);
    if (!asset)
        return false;
    Artwork a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.kind = "relief";
    a.name = QFileInfo(url.toLocalFile()).completeBaseName();
    a.layer = 0;
    a.reliefAsset = asset;
    a.bounds = QRectF(10, 10, width * .1, height * .1);
    a.settings = {{"speed", m_layers[0].toMap()["speed"]}, {"power", m_layers[0].toMap()["power"]}};
    if (!canAppend(a))
        return false;
    appendArtwork(a);
    notify("3D-Oberseite als 16-Bit-Tiefenkarte importiert. Modell vorher extern ausrichten; keine "
           "Übernahme realer Z-Maße.");
    return true;
}
