#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class MaterialLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
  public:
    explicit MaterialLibrary(QString storagePath = {}, QObject *parent = nullptr);
    QStringList names() const;
    int count() const { return m_profiles.size(); }
    QString message() const { return m_message; }
    Q_INVOKABLE QVariantMap profileAt(int row) const;
    Q_INVOKABLE int indexOf(const QString &id) const;
    Q_INVOKABLE QString saveProfile(QVariantMap profile);
    Q_INVOKABLE QString duplicateProfile(int row);
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE QVariantMap newProfile() const {
        auto p = example();
        p["id"] = "";
        return p;
    }
    static QVariantMap example();
    static bool validate(const QVariantMap &profile, QString &error);
  signals:
    void changed();

  private:
    bool persist(const QVariantList &profiles);
    void message(const QString &text);
    QVariantList m_profiles;
    QString m_path, m_message;
    bool m_storageBlocked = false;
};
