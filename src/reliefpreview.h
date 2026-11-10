#pragma once
#include "document.h"
#include <QPointer>
#include <QQuickPaintedItem>

class ReliefPreview : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int slice READ slice WRITE setSlice NOTIFY viewChanged)
    Q_PROPERTY(bool threeD READ threeD WRITE setThreeD NOTIFY viewChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY viewChanged)
  public:
    explicit ReliefPreview(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {}
    Document *document() const { return m_document; }
    void setDocument(Document *document);
    int slice() const { return m_slice; }
    void setSlice(int value) {
        m_slice = value;
        update();
        emit viewChanged();
    }
    bool threeD() const { return m_threeD; }
    void setThreeD(bool value) {
        m_threeD = value;
        update();
        emit viewChanged();
    }
    bool dark() const { return m_dark; }
    void setDark(bool value) {
        m_dark = value;
        update();
        emit viewChanged();
    }
    void paint(QPainter *painter) override;
  signals:
    void documentChanged();
    void viewChanged();

  private:
    QPointer<Document> m_document;
    int m_slice = 0;
    bool m_threeD = true, m_dark = false;
};
