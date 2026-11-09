#pragma once
#include <QAbstractListModel>
#include <QColor>
#include <QPainterPath>
#include <QUrl>
#include <QVariantMap>

struct Artwork {
    QString id, name, kind, text;
    QRectF bounds;
    int layer = 0;
    bool visible = true;
    QByteArray svg;
};

class Document : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(double pathLength READ pathLength NOTIFY changed)
  public:
    enum Roles { NameRole = Qt::UserRole + 1, KindRole, ColorRole, VisibleRole };
    explicit Document(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_items.size(); }
    int selected() const { return m_selected; }
    void select(int row);
    QVariantMap selection() const;
    QVariantList layers() const;
    QString title() const { return m_title; }
    QString message() const { return m_message; }
    bool dirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    double pathLength() const;
    const QList<Artwork> &items() const { return m_items; }
    QColor layerColor(int layer) const;
    static QPainterPath outline(const Artwork &item);
    Q_INVOKABLE void demo();
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void addShape(const QString &kind);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void duplicateSelected();
    Q_INVOKABLE void toggleVisible(int row);
    Q_INVOKABLE void setProperty(const QString &name, const QVariant &value);
    Q_INVOKABLE void setLayer(int layer);
    Q_INVOKABLE void setLayerSetting(int layer, const QString &key, const QVariant &value);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool importSvg(const QUrl &url);
    Q_INVOKABLE bool save(const QUrl &url);
    Q_INVOKABLE bool open(const QUrl &url);
    void beginMove();
    void moveSelection(QPointF position);
    void finishMove();
    void notify(const QString &message);
  signals:
    void changed();
    void selectionChanged();
    void messageChanged();

  private:
    QByteArray state() const;
    bool restore(const QByteArray &json);
    void checkpoint();
    void refresh();
    QList<Artwork> m_items;
    QVariantList m_layers;
    int m_selected = -1;
    QString m_title = "Werkstattschild", m_message;
    bool m_dirty = false;
    QList<QByteArray> m_undo, m_redo;
    QByteArray m_beforeMove;
};
