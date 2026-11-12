#pragma once
#include "document.h"
#include "simulator.h"
#include <QSettings>
#include <memory>
class ApplicationServices : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY changed)
  public:
    ApplicationServices(Document *document, Simulator *machine, const QString &settingsFile = {},
                        QObject *parent = nullptr);
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
  signals:
    void changed();

  private:
    Document *m_document;
    Simulator *m_machine;
    std::unique_ptr<QSettings> m_settings;
    bool m_complete = false;
    QString m_message;
    QStringList m_recent;
    bool storeRecentProjects(const QStringList &files);
};
