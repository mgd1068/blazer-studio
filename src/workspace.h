#pragma once
#include "document.h"
#include "simulator.h"
#include <QCache>
#include <QPointer>
#include <QQuickPaintedItem>
class Workspace : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Simulator *simulator READ simulator WRITE setSimulator NOTIFY simulatorChanged)
    Q_PROPERTY(bool preview READ preview WRITE setPreview NOTIFY previewChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY zoomChanged)
  public:
    explicit Workspace(QQuickItem *p = nullptr);
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
    void simulatorChanged();
    void previewChanged();
    void darkChanged();
    void zoomChanged();

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
    QPointer<Document> m_document;
    QPointer<Simulator> m_simulator;
    bool m_preview = false, m_dark = false, m_drag = false, m_panning = false;
    double m_zoom = 1;
    QPointF m_pan, m_last, m_offset;
    QCache<QString, QPainterPath> m_textPaths{128};
};
