#include "workspace.h"
#include "geometry.h"
#include "imageprocessing.h"
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
        connect(d, &Document::changed, this, [this] {
            bool selected = false, draft = m_guideDraft && m_guideDraft->id.isEmpty();
            for (const auto &g : m_document->allGuides())
                if (g.page == m_document->activePage()) {
                    selected |= g.id == m_selectedGuide;
                    if (m_guideDraft && g.id == m_guideDraft->id && !g.locked)
                        draft = true;
                }
            if (m_guideDraft && (!draft || m_guideDraft->page != m_document->activePage()))
                cancelGuideDrag();
            if (!selected && !m_selectedGuide.isEmpty()) {
                m_selectedGuide.clear();
                emit selectedGuideChanged();
            }
            update();
        });
        connect(d, &Document::selectionChanged, this, [this] {
            if (m_document->selected() >= 0 && !m_selectedGuide.isEmpty()) {
                m_selectedGuide.clear();
                emit selectedGuideChanged();
            }
            ensureSelectionVisible();
            update();
        });
    }
    emit documentChanged();
    update();
}
void Workspace::setSimulator(Simulator *s) {
    if (m_simulator)
        disconnect(m_simulator, nullptr, this, nullptr);
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
QPointF Workspace::toWorld(QPointF p) const {
    return Geometry::worldToCanvas((p - origin()) / scale());
}
void Workspace::zoomBy(double factor) {
    if (!std::isfinite(factor) || factor <= 0)
        return;
    m_zoom = std::clamp(m_zoom * factor, .02, 5.);
    update();
    emit zoomChanged();
}
QRectF Workspace::selectedScreenBounds() const {
    if (!m_document || m_document->selected() < 0)
        return {};
    const auto r = Geometry::worldToCanvas(m_document->selectionBounds());
    return {origin() + r.topLeft() * scale(), r.size() * scale()};
}
void Workspace::ensureSelectionVisible() {
    if (!m_document || m_document->selected() < 0 || width() < 100 || height() < 100)
        return;
    const QRectF visible(28, 28, width() - 56, height() - 90);
    auto selected = selectedScreenBounds().adjusted(-8, -8, 8, 8);
    if (visible.contains(selected))
        return;
    const double factor =
        std::min(visible.width() / selected.width(), visible.height() / selected.height());
    if (factor < 1) {
        m_zoom = std::clamp(m_zoom * factor * .95, .02, 5.);
        emit zoomChanged();
    }
    selected = selectedScreenBounds();
    m_pan += visible.center() - selected.center();
    update();
}
void Workspace::geometryChange(const QRectF &now, const QRectF &before) {
    QQuickPaintedItem::geometryChange(now, before);
    ensureSelectionVisible();
}
void Workspace::wheelEvent(QWheelEvent *e) {
    zoomBy(e->angleDelta().y() > 0 ? 1.12 : 1 / 1.12);
    e->accept();
}
void Workspace::mousePressEvent(QMouseEvent *e) {
    forceActiveFocus(Qt::MouseFocusReason);
    m_last = e->position();
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        return;
    }
    if (!m_document || m_preview)
        return;
    auto pos = toWorld(e->position());
    const auto o = origin();
    const auto s = scale();
    const bool fromTop = QRectF(o.x(), o.y() - 28, 200 * s, 26).contains(e->position());
    const bool fromLeft = QRectF(o.x() - 40, o.y(), 38, 150 * s).contains(e->position());
    if (e->button() == Qt::LeftButton && (fromTop || fromLeft)) {
        m_document->select(-1);
        m_selectedGuide.clear();
        m_guideDraft = GuideDraft{{},
                                  fromTop ? "horizontal" : "vertical",
                                  fromTop ? pos.y() : pos.x(),
                                  m_document->activePage()};
        emit selectedGuideChanged();
        update();
        return;
    }
    if (m_nodes) {
        for (auto node : m_document->nodes()) {
            auto n = node.toMap();
            if (QLineF(pos, QPointF(n["x"].toDouble(), n["y"].toDouble())).length() < 7 / scale()) {
                m_dragNode = n["index"].toInt();
                m_document->beginMove();
                return;
            }
        }
    }
    for (int i = m_document->items().size() - 1; i >= 0; --i) {
        auto &a = m_document->items()[i];
        if (a.page == m_document->activePage() && a.visible &&
            a.bounds.adjusted(-1, -1, 1, 1).contains(pos)) {
            const bool toggle = e->modifiers().testFlag(Qt::ControlModifier) ||
                                e->modifiers().testFlag(Qt::ShiftModifier);
            if (toggle)
                m_document->toggleSelection(i);
            else if (!a.groupId.isEmpty() && !e->modifiers().testFlag(Qt::AltModifier))
                m_document->selectGroup(i);
            else
                m_document->select(i);
            if (!m_document->isSelected(i))
                return;
            // Auto-reveal may have changed the viewport during selection.
            m_offset = toWorld(e->position()) - m_document->selectionBounds().topLeft();
            m_document->beginMove();
            m_drag = true;
            return;
        }
    }
    if (m_document->guidesVisible())
        for (const auto &guide : m_document->allGuides())
            if (guide.page == m_document->activePage()) {
                const double distance =
                    std::abs(guide.position - (guide.axis == "vertical" ? pos.x() : pos.y())) *
                    scale();
                if (distance <= 5) {
                    setSelectedGuideId(guide.id);
                    if (!guide.locked)
                        m_guideDraft = GuideDraft{guide.id, guide.axis, guide.position, guide.page};
                    emit selectedGuideChanged();
                    update();
                    return;
                }
            }
    setSelectedGuideId({});
    m_document->select(-1);
}
void Workspace::mouseMoveEvent(QMouseEvent *e) {
    if (m_guideDraft && m_document) {
        const auto p = toWorld(e->position());
        m_guideDraft->position = m_guideDraft->axis == "vertical" ? p.x() : p.y();
        m_guideDraft->position = std::round(m_guideDraft->position / snapStep()) * snapStep();
        update();
        return;
    }
    m_alignmentX.reset();
    m_alignmentY.reset();
    if (m_dragNode >= 0 && m_document) {
        auto p = toWorld(e->position());
        m_document->setNode(m_dragNode, p.x(), p.y());
    } else if (m_panning) {
        m_pan += e->position() - m_last;
        m_last = e->position();
        update();
    } else if (m_drag && m_document) {
        auto pos = toWorld(e->position()) - m_offset;
        pos.setX(std::round(pos.x() / snapStep()) * snapStep());
        pos.setY(std::round(pos.y() / snapStep()) * snapStep());
        if (!e->modifiers().testFlag(Qt::AltModifier)) {
            QList<double> xs{0, 100, 200}, ys{0, 75, 150};
            for (int i = 0; i < m_document->items().size(); ++i) {
                const auto &a = m_document->items()[i];
                if (a.visible && a.page == m_document->activePage() && !m_document->isSelected(i)) {
                    xs << a.bounds.left() << a.bounds.center().x() << a.bounds.right();
                    ys << a.bounds.top() << a.bounds.center().y() << a.bounds.bottom();
                }
            }
            if (m_document->guidesVisible() && m_document->guideSnapping())
                for (const auto &g : m_document->allGuides())
                    if (g.page == m_document->activePage())
                        (g.axis == "vertical" ? xs : ys).append(g.position);
            auto snap = [&](double value, double extent, const QList<double> &targets,
                            std::optional<double> &alignment) {
                double best = 5 / scale(), result = value;
                for (double target : targets)
                    for (double anchor : {0., extent / 2, extent}) {
                        double delta = target - (value + anchor);
                        if (std::abs(delta) < best) {
                            best = std::abs(delta);
                            result = value + delta;
                            alignment = target;
                        }
                    }
                return result;
            };
            auto size = m_document->selectionBounds().size();
            pos.setX(snap(pos.x(), size.width(), xs, m_alignmentX));
            pos.setY(snap(pos.y(), size.height(), ys, m_alignmentY));
        }

        m_document->moveSelection(pos);
    }
}
void Workspace::mouseUngrabEvent() {
    cancelGuideDrag();
    m_alignmentX.reset();
    m_alignmentY.reset();
    if ((m_drag || m_dragNode >= 0) && m_document)
        m_document->finishMove();
    m_dragNode = -1;
    m_drag = m_panning = false;
}
void Workspace::mouseReleaseEvent(QMouseEvent *event) {
    if (m_guideDraft && m_document) {
        auto draft = *m_guideDraft;
        const auto world = toWorld(event->position());
        draft.position =
            std::round((draft.axis == "vertical" ? world.x() : world.y()) / snapStep()) *
            snapStep();
        m_guideDraft.reset();
        emit selectedGuideChanged();
        const bool outside = !boundingRect().contains(event->position()) ||
                             event->position().x() < origin().x() - 4 ||
                             event->position().y() < origin().y() - 4;
        if (draft.page == m_document->activePage()) {
            if (outside) {
                if (!draft.id.isEmpty())
                    m_document->removeGuide(draft.id);
            } else if (draft.id.isEmpty())
                setSelectedGuideId(m_document->addGuide(draft.axis, draft.position));
            else
                m_document->setGuidePosition(draft.id, draft.position);
        }
        update();
        return;
    }
    m_alignmentX.reset();
    m_alignmentY.reset();
    if ((m_drag || m_dragNode >= 0) && m_document)
        m_document->finishMove();
    m_dragNode = -1;
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
    if (m_registration && m_registration->visible() && !m_preview) {
        p->save();
        p->setClipRect(bed);
        p->setOpacity(.45);
        QTransform world;
        world.translate(o.x(), o.y() + 150 * s);
        world.scale(s, -s);
        p->setTransform(m_registration->transform() * world);
        p->drawImage(QPointF(0, 0), m_registration->frame());
        p->restore();
    }
    p->save();
    p->setClipRect(bed);
    p->setPen(QPen(m_dark ? QColor("#35434a") : QColor("#eef0e9"), 1));
    const double minor = m_imperial ? 2.54 : 5, major = m_imperial ? 25.4 : 25;
    for (double x = 0; x <= 200; x += minor)
        p->drawLine(QPointF(o.x() + x * s, o.y()), QPointF(o.x() + x * s, o.y() + 150 * s));
    for (double y = 0; y <= 150; y += minor)
        p->drawLine(QPointF(o.x(), o.y() + (150 - y) * s),
                    QPointF(o.x() + 200 * s, o.y() + (150 - y) * s));
    p->setPen(QPen(m_dark ? QColor("#43515a") : QColor("#dde3d9"), 1));
    for (double x = 0; x <= 200; x += major)
        p->drawLine(QPointF(o.x() + x * s, o.y()), QPointF(o.x() + x * s, o.y() + 150 * s));
    for (double y = 0; y <= 150; y += major)
        p->drawLine(QPointF(o.x(), o.y() + (150 - y) * s),
                    QPointF(o.x() + 200 * s, o.y() + (150 - y) * s));
    if (m_document) {
        p->setClipRect(boundingRect());
        p->translate(o);
        p->scale(s, s);
        if (m_preview) {
            for (const auto &move : m_document->job().moves) {
                QPen pen((move.rapid || move.power <= 0) ? QColor("#9cafb9")
                                                         : m_document->layerColor(move.layer),
                         (move.rapid || move.power <= 0) ? .7 : 1.,
                         (move.rapid || move.power <= 0) ? Qt::DashLine : Qt::SolidLine);
                pen.setCosmetic(true);
                p->setPen(pen);
                p->drawLine(Geometry::worldToCanvas(move.from), Geometry::worldToCanvas(move.to));
            }
        } else {
            int idx = 0;
            QPointF previous(0, 150);
            for (const auto &world : m_document->items()) {
                if (world.page != m_document->activePage()) {
                    ++idx;
                    continue;
                }
                Artwork a = world;
                a.bounds = Geometry::worldToCanvas(world.bounds);
                const bool enabled = m_document->layers()[a.layer].toMap()["enabled"].toBool();
                const bool selected = m_document->isSelected(idx);
                if ((!a.visible && (!selected || m_preview)) || (!enabled && m_preview)) {
                    ++idx;
                    continue;
                }
                QColor c = a.toolLayer == 1 ? QColor("#d18228") : a.toolLayer == 2 ? QColor("#ad63b0") : m_document->layerColor(a.layer);
                p->setOpacity(!a.visible ? .18 : (a.toolLayer != 0 || enabled) ? 1 : .3);
                if (m_preview) {
                    p->setPen(QPen(QColor("#a4b6bb"), .35, Qt::DashLine));
                    p->drawLine(previous, a.bounds.topLeft());
                    previous = a.bounds.topLeft();
                }
                p->setPen(QPen(c, .45, a.toolLayer != 0 ? Qt::DashLine : Qt::SolidLine));
                p->setBrush(Qt::NoBrush);
                if (a.kind == "path") {
                    QString error;
                    auto path = JobPlanner::geometry(world, error, true);
                    QTransform t;
                    t.translate(0, 150);
                    t.scale(1, -1);
                    if (a.toolLayer != 0 || m_document->layers()[a.layer].toMap()["mode"] == "Schnitt")
                        p->drawPath(t.map(path));
                    else
                        p->fillPath(t.map(path), c);
                } else if (a.kind == "rect")
                    p->drawRect(a.bounds);
                else if (a.kind == "ellipse")
                    p->drawEllipse(a.bounds);
                else if ((a.kind == "relief" || a.kind == "image") && a.reliefAsset) {
                    QString error;
                    auto image =
                        a.kind == "image" ? m_document->imagePreview(world) : a.reliefAsset->image;
                    p->drawImage(a.bounds, image);
                } else if (a.kind == "svg") {
                    if (a.asset)
                        a.asset->renderer->render(p, a.bounds);
                } else if (a.kind == "text") {
                    QString error;
                    auto path = JobPlanner::geometry(world, error, true);
                    QTransform t;
                    t.translate(0, 150);
                    t.scale(1, -1);
                    p->fillPath(t.map(path), c);
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
                } else if (selected) {
                    p->setBrush(Qt::NoBrush);
                    p->setPen(QPen(QColor("#1c956f"), .35, Qt::DashLine));
                    p->drawRect(a.bounds.adjusted(-1.5, -1.5, 1.5, 1.5));
                    p->setBrush(Qt::white);
                    p->setPen(QPen(QColor("#1c956f"), .4));
                    for (auto point : {a.bounds.topLeft(), a.bounds.topRight(),
                                       a.bounds.bottomLeft(), a.bounds.bottomRight()})
                        p->drawRect(QRectF(point - QPointF(.9, .9), QSizeF(1.8, 1.8)));
                }
                ++idx;
            }
            if (m_nodes) {
                p->setPen(QPen(QColor("#158c71"), .25));
                p->setBrush(QColor("#ffffff"));
                for (auto node : m_document->nodes()) {
                    auto n = node.toMap();
                    auto point =
                        Geometry::worldToCanvas(QPointF(n["x"].toDouble(), n["y"].toDouble()));
                    p->drawEllipse(point, 2 / scale(), 2 / scale());
                }
            }
            if (!m_preview && m_document->selectedRows().size() > 1) {
                p->setPen(QPen(QColor("#1c956f"), .6, Qt::DashLine));
                p->setBrush(Qt::NoBrush);
                p->drawRect(
                    Geometry::worldToCanvas(m_document->selectionBounds()).adjusted(-2, -2, 2, 2));
            }
        }
        p->resetTransform();
    }
    p->restore();
    p->setPen(QPen(m_dark ? QColor("#5c6e7b") : QColor("#aab9bf"), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRect(bed);
    p->setFont(QFont("DejaVu Sans", 8));
    p->setPen(m_dark ? QColor("#9cabb4") : QColor("#768994"));
    for (double x = 0; x <= 200; x += major) {
        auto px = o.x() + x * s;
        p->drawText(QRectF(px - 15, o.y() - 24, 30, 15), Qt::AlignCenter,
                    QString::number(m_imperial ? qRound(x / 25.4) : qRound(x)));
    }
    for (double y = 0; y <= 150; y += major) {
        const auto py = o.y() + (150 - y) * s;
        p->drawText(QRectF(o.x() - 36, py - 7, 25, 14), Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(m_imperial ? qRound(y / 25.4) : qRound(y)));
    }
    paintGuides(p);
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

void Workspace::setSelectedGuideId(const QString &id) {
    QString next;
    if (m_document)
        for (const auto &g : m_document->allGuides())
            if (g.id == id && g.page == m_document->activePage()) {
                next = id;
                break;
            }
    if (next == m_selectedGuide)
        return;
    m_selectedGuide = next;
    if (!next.isEmpty())
        m_document->select(-1);
    emit selectedGuideChanged();
    update();
}
void Workspace::cancelGuideDrag() {
    if (!m_guideDraft)
        return;
    m_guideDraft.reset();
    emit selectedGuideChanged();
    update();
}
void Workspace::paintGuides(QPainter *p) {
    if (!m_document || m_preview)
        return;
    p->save();
    p->setClipRect(boundingRect());
    const auto o = origin();
    const auto s = scale();
    const auto line = [&](const QString &axis, double position, QColor color, Qt::PenStyle style) {
        p->setPen(QPen(color, 1, style));
        if (axis == "vertical") {
            const double x = o.x() + position * s;
            p->drawLine(QPointF(x, 0), QPointF(x, height() - 45));
        } else {
            const double y = o.y() + (150 - position) * s;
            p->drawLine(QPointF(0, y), QPointF(width(), y));
        }
    };
    if (m_document->guidesVisible())
        for (const auto &g : m_document->allGuides())
            if (g.page == m_document->activePage() && (!m_guideDraft || g.id != m_guideDraft->id))
                line(g.axis, g.position,
                     g.id == m_selectedGuide ? QColor("#c98720") : QColor("#548fae"),
                     g.locked ? Qt::DotLine : Qt::DashLine);
    if (m_guideDraft) {
        line(m_guideDraft->axis, m_guideDraft->position, QColor("#dc9b38"), Qt::DashLine);
        const auto label = QString::number(m_guideDraft->position / (m_imperial ? 25.4 : 1), 'f',
                                           m_imperial ? 4 : 2) +
                           (m_imperial ? " in" : " mm");
        p->setPen(m_dark ? Qt::white : Qt::black);
        p->drawText(QRectF(45, 8, 200, 24), label);
    }
    if (m_drag) {
        if (m_alignmentX)
            line("vertical", *m_alignmentX, QColor("#be6b9c"), Qt::DashDotLine);
        if (m_alignmentY)
            line("horizontal", *m_alignmentY, QColor("#be6b9c"), Qt::DashDotLine);
    }
    p->restore();
}
