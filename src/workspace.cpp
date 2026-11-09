#include "workspace.h"
#include <QMouseEvent>
#include <QPainter>
#include <QSvgRenderer>
#include <QWheelEvent>
#include <algorithm>
Workspace::Workspace(QQuickItem *p) : QQuickPaintedItem(p) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAntialiasing(true);
    setClip(true);
}
void Workspace::setDocument(Document *d) {
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = d;
    if (d) {
        connect(d, &Document::changed, this, [this] { update(); });
        connect(d, &Document::selectionChanged, this, [this] { update(); });
    }
    emit documentChanged();
    update();
}
void Workspace::setSimulator(Simulator *s) {
    m_simulator = s;
    if (s)
        connect(s, &Simulator::changed, this, [this] { update(); });
    emit simulatorChanged();
}
double Workspace::scale() const {
    return std::max(.1, std::min((width() - 110) / 200., (height() - 110) / 150.)) * m_zoom;
}
QPointF Workspace::origin() const {
    return QPointF((width() - 200 * scale()) / 2, (height() - 150 * scale()) / 2) + m_pan;
}
void Workspace::zoomBy(double factor) {
    m_zoom = std::clamp(m_zoom * factor, .45, 5.);
    update();
    emit zoomChanged();
}
void Workspace::wheelEvent(QWheelEvent *e) {
    zoomBy(e->angleDelta().y() > 0 ? 1.12 : 1 / 1.12);
    e->accept();
}
void Workspace::mousePressEvent(QMouseEvent *e) {
    m_last = e->position();
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        return;
    }
    if (!m_document || m_preview)
        return;
    auto pos = toWorld(e->position());
    for (int i = m_document->items().size() - 1; i >= 0; --i) {
        auto &a = m_document->items()[i];
        if (a.visible && a.bounds.adjusted(-1, -1, 1, 1).contains(pos)) {
            m_document->select(i);
            m_offset = pos - a.bounds.topLeft();
            m_document->beginMove();
            m_drag = true;
            return;
        }
    }
    m_document->select(-1);
}
void Workspace::mouseMoveEvent(QMouseEvent *e) {
    if (m_panning) {
        m_pan += e->position() - m_last;
        m_last = e->position();
        update();
    } else if (m_drag && m_document) {
        auto pos = toWorld(e->position()) - m_offset;
        pos.setX(std::round(pos.x() * 2) / 2);
        pos.setY(std::round(pos.y() * 2) / 2);
        m_document->moveSelection(pos);
    }
}
void Workspace::mouseReleaseEvent(QMouseEvent *) {
    if (m_drag && m_document)
        m_document->finishMove();
    m_drag = m_panning = false;
}
void Workspace::paint(QPainter *p) {
    p->setRenderHint(QPainter::Antialiasing);
    p->fillRect(boundingRect(), m_dark ? QColor("#202b35") : QColor("#e9eef1"));
    auto o = origin();
    auto s = scale();
    QRectF bed(o, QSizeF(200 * s, 150 * s));
    p->setPen(Qt::NoPen);
    p->setBrush(m_dark ? QColor("#162028") : QColor("#d3dce0"));
    p->drawRoundedRect(bed.translated(4, 6), 3, 3);
    p->setBrush(m_dark ? QColor("#29363e") : QColor("#fcfcf9"));
    p->drawRect(bed);
    p->save();
    p->setClipRect(bed);
    p->setPen(QPen(m_dark ? QColor("#35434a") : QColor("#eef0e9"), 1));
    for (int x = 0; x <= 200; x += 5)
        p->drawLine(QPointF(o.x() + x * s, o.y()), QPointF(o.x() + x * s, o.y() + 150 * s));
    for (int y = 0; y <= 150; y += 5)
        p->drawLine(QPointF(o.x(), o.y() + y * s), QPointF(o.x() + 200 * s, o.y() + y * s));
    p->setPen(QPen(m_dark ? QColor("#43515a") : QColor("#dde3d9"), 1));
    for (int x = 0; x <= 200; x += 25)
        p->drawLine(QPointF(o.x() + x * s, o.y()), QPointF(o.x() + x * s, o.y() + 150 * s));
    for (int y = 0; y <= 150; y += 25)
        p->drawLine(QPointF(o.x(), o.y() + y * s), QPointF(o.x() + 200 * s, o.y() + y * s));
    if (m_document) {
        p->translate(o);
        p->scale(s, s);
        int idx = 0;
        QPointF previous(0, 150);
        for (const auto &a : m_document->items()) {
            const bool enabled = m_document->layers()[a.layer].toMap()["enabled"].toBool();
            if (!a.visible || (!enabled && m_preview)) {
                ++idx;
                continue;
            }
            QColor c = m_document->layerColor(a.layer);
            p->setOpacity(enabled ? 1 : .3);
            if (m_preview) {
                p->setPen(QPen(QColor("#a4b6bb"), .35, Qt::DashLine));
                p->drawLine(previous, a.bounds.topLeft());
                previous = a.bounds.topLeft();
            }
            p->setPen(QPen(c, .45));
            p->setBrush(Qt::NoBrush);
            if (a.kind == "rect")
                p->drawRect(a.bounds);
            else if (a.kind == "ellipse")
                p->drawEllipse(a.bounds);
            else if (a.kind == "svg") {
                QSvgRenderer renderer(a.svg);
                renderer.render(p, a.bounds);
            } else if (a.kind == "text") {
                QFont font("DejaVu Sans");
                font.setWeight(a.text == "MOIN" ? QFont::Black : QFont::DemiBold);
                font.setPixelSize(100);
                QPainterPath path;
                path.addText(0, 100, font, a.text);
                auto b = path.boundingRect();
                if (!b.isEmpty()) {
                    QTransform t;
                    t.translate(a.bounds.x(), a.bounds.y());
                    t.scale(a.bounds.width() / b.width(), a.bounds.height() / b.height());
                    t.translate(-b.x(), -b.y());
                    p->fillPath(t.map(path), c);
                }
            }
            p->setOpacity(1);
            if (m_preview) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor("#1c956f"));
                p->drawEllipse(a.bounds.topLeft(), 1.4, 1.4);
                p->setPen(Qt::white);
                QFont n("DejaVu Sans");
                n.setPixelSize(2);
                p->setFont(n);
                p->drawText(QRectF(a.bounds.topLeft() - QPointF(1.4, 1.4), QSizeF(2.8, 2.8)),
                            Qt::AlignCenter, QString::number(idx + 1));
            } else if (idx == m_document->selected()) {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(QColor("#1c956f"), .35, Qt::DashLine));
                p->drawRect(a.bounds.adjusted(-1.5, -1.5, 1.5, 1.5));
                p->setBrush(Qt::white);
                p->setPen(QPen(QColor("#1c956f"), .4));
                for (auto point : {a.bounds.topLeft(), a.bounds.topRight(), a.bounds.bottomLeft(),
                                   a.bounds.bottomRight()})
                    p->drawRect(QRectF(point - QPointF(.9, .9), QSizeF(1.8, 1.8)));
            }
            ++idx;
        }
        p->resetTransform();
    }
    p->restore();
    p->setPen(QPen(m_dark ? QColor("#5c6e7b") : QColor("#aab9bf"), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRect(bed);
    p->setFont(QFont("DejaVu Sans", 8));
    p->setPen(m_dark ? QColor("#9cabb4") : QColor("#768994"));
    for (int x = 0; x <= 200; x += 25) {
        auto px = o.x() + x * s;
        p->drawText(QRectF(px - 15, o.y() - 24, 30, 15), Qt::AlignCenter, QString::number(x));
    }
    for (int y = 0; y <= 150; y += 25) {
        auto py = o.y() + y * s;
        p->drawText(QRectF(o.x() - 36, py - 7, 25, 14), Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(150 - y));
    }
    p->setPen(QPen(QColor("#bc6955"), 2));
    p->drawLine(bed.bottomLeft(), bed.bottomLeft() + QPointF(18, 0));
    p->setPen(QPen(QColor("#378f7b"), 2));
    p->drawLine(bed.bottomLeft(), bed.bottomLeft() + QPointF(0, -18));
    if (m_simulator && m_simulator->connected()) {
        QPointF h = o + QPointF(m_simulator->x() * s, (150 - m_simulator->y()) * s);
        bool fire = m_simulator->firing();
        p->setPen(QPen(fire ? QColor("#d17a35") : QColor("#697f8d"), 1));
        p->drawLine(h - QPointF(9, 0), h + QPointF(9, 0));
        p->drawLine(h - QPointF(0, 9), h + QPointF(0, 9));
        p->setBrush(fire ? QColor("#f39d36") : Qt::transparent);
        p->drawEllipse(h, 4, 4);
    }
}
