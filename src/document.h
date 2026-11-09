#pragma once
#include "svgasset.h"
#include <QAbstractListModel>
#include <QColor>
#include <QPainterPath>
#include <QUrl>
#include <QVariantMap>
#include <optional>

struct Artwork {
    QString id, name, kind, text;
    QRectF bounds; // mm, lower-left origin, y points up
    int layer = 0;
    bool visible = true;
    std::shared_ptr<const SvgAsset> asset;
    bool operator==(const Artwork &) const = default;
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
    Q_PROPERTY(QVariantMap activeBounds READ activeBounds NOTIFY changed)
    Q_PROPERTY(QStringList issues READ issues NOTIFY changed)
    Q_PROPERTY(QVariantMap material READ material NOTIFY changed)
    Q_PROPERTY(bool materialModified READ materialModified NOTIFY changed)
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
    QVariantList layers() const { return m_layers; }
    QString title() const { return m_title; }
    QString message() const { return m_message; }
    bool dirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    double pathLength() const;
    QVariantMap activeBounds() const;
    QStringList issues() const;
    QVariantMap material() const { return m_material; }
    bool materialModified() const;
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
    Q_INVOKABLE bool applyMaterial(const QVariantMap &profile);
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
    struct Snapshot {
        QList<Artwork> items;
        QVariantList layers;
        QString title;
        int selected = -1;
        QVariantMap material;
    };
    Snapshot snapshot() const;
    bool sameContent(const Snapshot &a, const Snapshot &b) const;
    void applySnapshot(const Snapshot &state);
    QByteArray serialize() const;
    std::optional<Snapshot> parse(const QByteArray &json) const;
    void checkpoint();
    void refresh(int row = -1);
    void markClean();
    bool canAppend(const Artwork &item);
    void appendArtwork(const Artwork &item);
    QList<Artwork> m_items;
    QVariantList m_layers;
    QVariantMap m_material;
    int m_selected = -1;
    QString m_title = "Werkstattschild", m_message;
    bool m_dirty = false;
    Snapshot m_saved;
    QList<Snapshot> m_undo, m_redo;
    std::optional<Snapshot> m_beforeMove;
};
