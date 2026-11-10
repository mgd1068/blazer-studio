#pragma once
#include "document.h"
#include "registration.h"
#include "simulator.h"
#include <QCache>
#include <QPointer>
#include <QQuickPaintedItem>
class Workspace : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Registration *registration READ registration WRITE setRegistration NOTIFY
                   registrationChanged)
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Simulator *simulator READ simulator WRITE setSimulator NOTIFY simulatorChanged)
    Q_PROPERTY(bool preview READ preview WRITE setPreview NOTIFY previewChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(bool nodes READ nodeMode WRITE setNodeMode NOTIFY nodeModeChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)
  public:
    explicit Workspace(QQuickItem *p = nullptr);
    Registration *registration() const { return m_registration; }
    void setRegistration(Registration *r) {
        if (m_registration)
            disconnect(m_registration, nullptr, this, nullptr);
        m_registration = r;
        if (r)
            connect(r, &Registration::changed, this, [this] { update(); });
        emit registrationChanged();
        update();
    }
    Document *document() const { return m_document; }
    void setDocument(Document *d);
    Simulator *simulator() const { return m_simulator; }
    void setSimulator(Simulator *s);
    bool preview() const { return m_preview; }
    void setPreview(bool p) {
        m_preview = p;
        update();
        emit previewChanged();
    }
    bool dark() const { return m_dark; }
    void setDark(bool d) {
        m_dark = d;
        update();
        emit darkChanged();
    }
    bool nodeMode() const { return m_nodes; }
    void setNodeMode(bool v) {
        m_nodes = v;
        update();
        emit nodeModeChanged();
    }
    double zoom() const { return m_zoom; }
    Q_INVOKABLE void fit() {
        m_zoom = 1;
        m_pan = {};
        update();
        emit zoomChanged();
    }
    Q_INVOKABLE void zoomBy(double factor);
    Q_INVOKABLE void ensureSelectionVisible();
    QRectF selectedScreenBounds() const;
    void paint(QPainter *p) override;
  signals:
    void documentChanged();
    void registrationChanged();
    void simulatorChanged();
    void previewChanged();
    void darkChanged();
    void zoomChanged();
    void nodeModeChanged();

  protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mouseUngrabEvent() override;

  private:
    double scale() const;
    QPointF origin() const;
    QPointF toWorld(QPointF p) const;
    QPointer<Registration> m_registration;
    QPointer<Document> m_document;
    QPointer<Simulator> m_simulator;
    bool m_preview = false, m_dark = false, m_drag = false, m_panning = false;
    bool m_nodes = false;
    int m_dragNode = -1;
    double m_zoom = 1;
    QPointF m_pan, m_last, m_offset;
    QCache<QString, QPainterPath> m_textPaths{128};
};
