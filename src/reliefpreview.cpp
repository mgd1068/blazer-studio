#include "reliefpreview.h"
#include <QPainter>
#include <algorithm>

void ReliefPreview::setDocument(Document *document) {
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &Document::changed, this, [this] { update(); });
        connect(document, &Document::selectionChanged, this, [this] { update(); });
    }
    update();
    emit documentChanged();
}
void ReliefPreview::paint(QPainter *p) {
    p->fillRect(boundingRect(), m_dark ? QColor("#16232c") : QColor("#edf2f2"));
    p->setRenderHint(QPainter::Antialiasing);
    if (!m_document || m_document->selectedRows().size() != 1 ||
        m_document->items()[m_document->selected()].kind != "relief") {
        p->setPen(m_dark ? QColor("#b1c2ce") : QColor("#627c8e"));
        p->drawText(boundingRect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                    "Tiefenkarte importieren oder ein Reliefbild in der Objektliste auswählen.");
        return;
    }
    const auto &a = m_document->items()[m_document->selected()];
    if (!a.reliefAsset)
        return;
    const auto &settings = a.relief;
    const auto raster = ReliefRaster::build(*a.reliefAsset, settings, a.bounds.size());
    if (!raster.error.isEmpty()) {
        p->setPen(m_dark ? Qt::white : Qt::black);
        p->drawText(boundingRect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                    raster.error);
        return;
    }
    const int slice = std::clamp(m_slice, 0, settings.passes);
    if (!m_threeD || slice > 0) {
        QImage mask(raster.columns, raster.rows, QImage::Format_RGB32);
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 0; x < mask.width(); ++x) {
                int count = raster.counts[(raster.rows - 1 - y) * raster.columns + x];
                int gray = 255 - qRound(255. * count / settings.passes);
                mask.setPixelColor(x, y,
                                   slice > 0
                                       ? (count >= slice ? QColor("#cb713c") : QColor("#e7ecec"))
                                       : QColor(gray, gray, gray));
            }
        const QSizeF target =
            QSizeF(a.bounds.size()).scaled(width() - 40, height() - 40, Qt::KeepAspectRatio);
        p->drawImage(QRectF((width() - target.width()) / 2, (height() - target.height()) / 2,
                            target.width(), target.height()),
                     mask);
        return;
    }
    // Isometric display: removed material goes down. Vertical relief is exaggerated.
    const double maxSide = std::max(a.bounds.width(), a.bounds.height());
    const double ax = a.bounds.width() / maxSide, ay = a.bounds.height() / maxSide;
    const double span = ax + ay;
    const double scale = std::min((width() - 60) / span, (height() - 55) / (.4 * span + .35));
    const QPointF origin(width() / 2 - (ax - ay) * scale / 2, 22);
    auto vertex = [&](int x, int y) {
        int px = std::clamp(qRound(x / 40. * (raster.columns - 1)), 0, raster.columns - 1);
        int py = std::clamp(qRound(y / 40. * (raster.rows - 1)), 0, raster.rows - 1);
        double depth =
            double(raster.counts[(raster.rows - 1 - py) * raster.columns + px]) / settings.passes;
        return origin + QPointF((x / 40. * ax - y / 40. * ay) * scale,
                                ((x / 40. * ax + y / 40. * ay) * .4 + depth * .35) * scale);
    };
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            auto v = vertex(x, y);
            auto right = vertex(x + 1, y), down = vertex(x, y + 1);
            double shade = std::clamp(155 + (down.y() - v.y() - right.y() + v.y()) * 7, 70., 215.);
            QColor color(int(shade * .76), int(shade * .88), int(shade));
            p->setPen(QPen(color.darker(108), .4));
            p->setBrush(color);
            p->drawPolygon(QPolygonF{v, right, vertex(x + 1, y + 1), down});
        }
}
