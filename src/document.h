#pragma once
#include "batchexport.h"
#include "job.h"
#include "jobpreparation.h"
#include "relief.h"
#include "svgasset.h"
#include <QAbstractListModel>
#include <QColor>
#include <QPainterPath>
#include <QUrl>
#include <QVariantMap>
#include <optional>

class Document : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantMap beginnerSummary READ beginnerSummary NOTIFY changed)
    Q_PROPERTY(bool batchBusy READ batchBusy NOTIFY changed)
    Q_PROPERTY(QString planningState READ planningState NOTIFY changed)
    Q_PROPERTY(bool planningBusy READ planningBusy NOTIFY changed)
    Q_PROPERTY(QVariantMap jobSummary READ jobSummary NOTIFY changed)
    Q_PROPERTY(QStringList pages READ pages NOTIFY changed)
    Q_PROPERTY(int activePage READ activePage NOTIFY changed)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY selectionChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedRows READ selectedRows NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
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
    enum Roles {
        NameRole = Qt::UserRole + 1,
        KindRole,
        ColorRole,
        VisibleRole,
        LayerRole,
        GroupRole,
        GroupNameRole,
        GroupFirstRole,
        OnPageRole
    };
    explicit Document(QObject *parent = nullptr);
    void setBackgroundPlanning(bool enabled);
    bool backgroundPlanning() const { return m_background; }
    JobInput jobInput() const;
    Q_INVOKABLE void cancelPlanning();
    Q_INVOKABLE void retryPlanning();
    QString planningState() const;
    bool planningBusy() const;
    bool batchBusy() const { return m_batch.busy(); }
    QImage imagePreview(const Artwork &item) const;
    QStringList pages() const { return m_pages; }
    int activePage() const { return m_activePage; }
    QVariantList nodes() const;
    Q_INVOKABLE bool command(const QString &action, const QVariantMap &args = {});
    Q_INVOKABLE bool manageOperation(int row, const QString &action);
    Q_INVOKABLE int addOperation(const QString &name, const QString &mode);
    Q_INVOKABLE void assignOperations(const QVariantList &rows);
    Q_INVOKABLE bool addPage(const QString &name);
    Q_INVOKABLE bool copyPage(int page, const QString &name);
    Q_INVOKABLE void activatePage(int page);
    Q_INVOKABLE bool removePage(int page);
    bool outputs(const Artwork &item) const;
    Q_INVOKABLE bool renamePage(int page, const QString &name);
    QByteArray projectBytes() const { return serialize(); }
    bool restoreBytes(const QByteArray &bytes);
    Q_INVOKABLE bool importFile(const QUrl &url);
    Q_INVOKABLE bool importModel(const QUrl &url);
    Q_INVOKABLE bool exportSvg(const QUrl &url) const;
    Q_INVOKABLE bool importProject(const QUrl &url);
    Q_INVOKABLE bool setNode(int element, double x, double y);
    Q_INVOKABLE bool batch(const QUrl &csv, const QUrl &directory, const QString &pattern);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_items.size(); }
    int selected() const { return m_selected; }
    Q_INVOKABLE void select(int row);
    QVariantMap selection() const;
    QVariantList selectedRows() const;
    QRectF selectionBounds() const;
    bool isSelected(int row) const { return m_selection.contains(row); }
    Q_INVOKABLE void toggleSelection(int row);
    Q_INVOKABLE void selectGroup(int row);
    Q_INVOKABLE void groupSelection();
    Q_INVOKABLE void ungroupSelection();
    Q_INVOKABLE void scaleSelection(double percent);
    QVariantList layers() const { return m_layers; }
    QString title() const { return m_title; }
    QString notes() const { return m_notes; }
    QUrl fileUrl() const { return m_fileUrl; }
    Q_INVOKABLE bool setMetadata(const QString &title, const QString &notes);
    QString message() const { return m_message; }
    bool dirty() const { return m_dirty; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    double pathLength() const;
    const JobPlan &job() const;
    QVariantMap jobSummary() const;
    Q_INVOKABLE bool exportGcode(const QUrl &url);
    Q_INVOKABLE bool importImage(const QUrl &url);
    Q_INVOKABLE bool importRelief(const QUrl &url);
    Q_INVOKABLE void setReliefSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void setObjectSetting(const QString &key, double value);
    Q_INVOKABLE bool createMaterialTest(double minSpeed, double maxSpeed, double minPower,
                                        double maxPower, int steps, double moduleW);
    Q_INVOKABLE bool exportMaterialTest(const QUrl &url, double minSpeed, double maxSpeed,
                                        double minPower, double maxPower, int steps);
    QVariantMap activeBounds() const;
    QStringList issues() const;
    QVariantMap material() const { return m_material; }
    bool materialModified() const;
    const QList<Artwork> &items() const { return m_items; }
    Q_INVOKABLE QColor layerColor(int layer) const;
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
    QVariantMap beginnerSummary() const;
    Q_INVOKABLE bool applyBasicMaterial(const QVariantMap &profile);
    Q_INVOKABLE bool applyMaterial(const QVariantMap &profile);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool importSvg(const QUrl &url);
    Q_INVOKABLE bool save(const QUrl &url);
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE bool saveTemplate(const QUrl &url);
    Q_INVOKABLE bool newFromTemplate(const QUrl &url);
    void beginMove();
    void moveSelection(QPointF position);
    void finishMove();
    void notify(const QString &message);
  signals:
    void changed();
    void selectionChanged();
    void messageChanged();
    void projectFileUsed(const QUrl &file);
    void fileUrlChanged();

  private:
    struct Snapshot {
        QList<Artwork> items;
        QVariantList layers;
        QString title;
        int selected = -1;
        QVariantMap material;
        QList<int> selection;
        QStringList pages;
        int activePage = 0;
        QString notes;
    };
    Snapshot snapshot() const;
    bool sameContent(const Snapshot &a, const Snapshot &b) const;
    void applySnapshot(const Snapshot &state);
    QByteArray serialize() const;
    std::optional<Snapshot> parse(const QByteArray &json) const;
    void checkpoint();
    void refresh(int row = -1);
    void markClean();
    void setFileUrl(const QUrl &url);
    bool transformSelection(const QRectF &bounds, bool history = true);
    void normalizeGroups();
    bool canAppend(const Artwork &item);
    static bool validBudget(const QList<Artwork> &items);
    static bool bakePath(Artwork &item, const QPainterPath &path);
    void appendArtwork(const Artwork &item);
    QList<Artwork> m_items;
    QVariantList m_layers;
    QVariantMap m_material;
    int m_selected = -1;
    QList<int> m_selection;
    QStringList m_pages{"Blatt 01"};
    int m_activePage = 0;
    QString m_title = "Werkstattschild", m_notes, m_message;
    QUrl m_fileUrl;
    bool m_dirty = false;
    Snapshot m_saved;
    QList<Snapshot> m_undo, m_redo;
    std::optional<Snapshot> m_beforeMove;
    mutable std::optional<JobPlan> m_job;
    JobPreparation m_preparation;
    BatchExport m_batch;
    bool m_background = false;
};
