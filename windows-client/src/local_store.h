#pragma once

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

struct ChatMessage {
    qint64 id = 0;
    QString channelId;
    QString author;
    QString body;
    QDateTime createdAt;
    bool own = false;
};

// Each instance and its database connection belong to the creating thread.
class LocalStore {
public:
    explicit LocalStore(const QString& dataDirectory = {});
    ~LocalStore();

    LocalStore(const LocalStore &) = delete;
    LocalStore &operator=(const LocalStore &) = delete;

    bool open();
    QString errorString() const;
    QList<ChatMessage> messages(const QString &channelId) const;
    bool addMessage(const QString &channelId, const QString &author, const QString &body);
    QString profileName() const;
    void setProfileName(const QString &name);
    QString databasePath() const;

private:
    bool initialize();
    bool ensureOpen() const;

    QString m_connectionName;
    QString m_databasePath;
    QSqlDatabase m_database;
    mutable QString m_error;
};
