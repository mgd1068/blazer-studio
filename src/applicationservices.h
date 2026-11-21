#pragma once
#include "document.h"
#include "simulator.h"
#include <QSettings>
#include <memory>
class ApplicationServices : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lengthUnit READ lengthUnit NOTIFY changed)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY changed)
    Q_PROPERTY(bool beginnerMode READ beginnerMode NOTIFY changed)
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY changed)
  public:
    ApplicationServices(Document *document, Simulator *machine, const QString &settingsFile = {},
                        QObject *parent = nullptr);
    QString lengthUnit() const { return m_lengthUnit; }
    Q_INVOKABLE bool setLengthUnit(const QString &unit);
    bool darkTheme() const { return m_darkTheme; }
    bool beginnerMode() const { return m_beginnerMode; }
    Q_INVOKABLE bool setDarkTheme(bool enabled);
    Q_INVOKABLE bool setBeginnerMode(bool enabled);
    Q_INVOKABLE bool exportSettings(const QUrl &file);
    Q_INVOKABLE QVariantMap previewSettingsImport(const QUrl &file);
    Q_INVOKABLE bool importSettings(const QUrl &file, const QString &expectedDigest);
    bool firstRun() const { return !m_complete; }
    QString version() const;
    QString revision() const;
    QString message() const { return m_message; }
    Q_INVOKABLE bool finishOnboarding();
    Q_INVOKABLE QVariantMap diagnostics() const;
    Q_INVOKABLE bool exportDiagnostics(const QUrl &file);
    QVariantList recentProjects() const;
    Q_INVOKABLE bool clearRecentProjects();
    Q_INVOKABLE bool openRecentProject(const QUrl &file);
    Q_INVOKABLE void refreshRecentProjects() { emit changed(); }
    Q_INVOKABLE int reclaimFileDialogResources(QObject *dialog, QObject *owner);
  signals:
    void changed();

  private:
    Document *m_document;
    Simulator *m_machine;
    std::unique_ptr<QSettings> m_settings;
    bool m_complete = false;
    bool m_darkTheme = false;
    bool m_beginnerMode = true;
    QVariantMap portablePreferences() const;
    bool applyPortablePreferences(const QVariantMap &preferences);
    bool settingsFailure(const QString &message);
    QString m_message;
    QString m_lengthUnit="mm";
    QStringList m_recent;
    bool storeRecentProjects(const QStringList &files);
};
