#include "local_store.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace {

QString sqlError(const QString &context, const QSqlError &error)
{
    return context + QStringLiteral(": ") + error.text();
}

constexpr int maxBodyLength = 4000;

} // namespace

LocalStore::LocalStore(const QString& dataDirectory)
    : m_connectionName(QStringLiteral("nimbo-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    auto directory = dataDirectory;
    if (directory.isEmpty()) directory = qEnvironmentVariable("NIMBO_DATA_DIR");
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    if (!directory.isEmpty()) {
        m_databasePath = QDir(directory).absoluteFilePath(QStringLiteral("nimbo.sqlite"));
    }
}

LocalStore::~LocalStore()
{
    if (m_database.isValid()) {
        m_database.close();
    }
    m_database = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool LocalStore::open()
{
    m_error.clear();
    if (m_database.isOpen()) {
        return true;
    }
    if (m_databasePath.isEmpty()) {
        m_error = QStringLiteral("Не удалось определить папку данных Nimbo.");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(m_databasePath).absolutePath())) {
        m_error = QStringLiteral("Не удалось создать папку данных Nimbo.");
        return false;
    }
    if (!m_database.isValid()) {
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_database.setDatabaseName(m_databasePath);
        m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    }
    if (!m_database.open()) {
        m_error = sqlError(QStringLiteral("Не удалось открыть историю сообщений"), m_database.lastError());
        return false;
    }
    if (!initialize()) {
        m_database.close();
        return false;
    }
    return true;
}

bool LocalStore::initialize()
{
    if (!m_database.transaction()) {
        m_error = sqlError(QStringLiteral("Не удалось начать настройку базы"), m_database.lastError());
        return false;
    }
    QSqlQuery versionQuery(m_database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        m_error = sqlError(QStringLiteral("Не удалось прочитать версию базы"), versionQuery.lastError());
        m_database.rollback();
        return false;
    }
    const auto version = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (version > 1) {
        m_error = QStringLiteral("Эта база создана более новой версией Nimbo.");
        m_database.rollback();
        return false;
    }

    const auto execute = [this](const QString &statement) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            m_error = sqlError(QStringLiteral("Не удалось настроить базу"), query.lastError());
            return false;
        }
        return true;
    };
    if (!execute(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "channel_id TEXT NOT NULL, author TEXT NOT NULL, "
            "body TEXT NOT NULL CHECK(length(body) BETWEEN 1 AND 4000), "
            "created_at TEXT NOT NULL, is_own INTEGER NOT NULL CHECK(is_own IN (0, 1)))"))
        || !execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS messages_channel_date ON messages(channel_id, created_at, id)"))
        || !execute(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))) {
        m_database.rollback();
        return false;
    }

    if (version == 0) {
        struct WelcomeMessage {
            QString channel;
            QString body;
        };
        const WelcomeMessage welcome[] = {
            {QStringLiteral("general"), QStringLiteral(
                "Это локальный прототип Nimbo. Сообщения сохраняются только на этом компьютере; "
                "подключения к серверу и голосовой связи пока нет.")},
            {QStringLiteral("general"), QStringLiteral(
                "Выберите канал, задайте имя в профиле и напишите первое сообщение. "
                "История останется после перезапуска.")},
            {QStringLiteral("ideas"), QStringLiteral(
                "Здесь можно записывать идеи для Nimbo: голосовые комнаты, настройки микрофона "
                "и удобные приглашения.")},
            {QStringLiteral("updates"), QStringLiteral(
                "Первый локальный прототип: каналы, профиль и история сообщений. "
                "Остальные возможности появятся после их реализации.")},
        };
        QSqlQuery insert(m_database);
        if (!insert.prepare(QStringLiteral(
                "INSERT INTO messages(channel_id, author, body, created_at, is_own) "
                "VALUES(:channel, :author, :body, :created, 0)"))) {
            m_error = sqlError(QStringLiteral("Не удалось подготовить вводные сообщения"), insert.lastError());
            m_database.rollback();
            return false;
        }
        const auto createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        for (const auto &message : welcome) {
            insert.bindValue(QStringLiteral(":channel"), message.channel);
            insert.bindValue(QStringLiteral(":author"), QStringLiteral("Nimbo"));
            insert.bindValue(QStringLiteral(":body"), message.body);
            insert.bindValue(QStringLiteral(":created"), createdAt);
            if (!insert.exec()) {
                m_error = sqlError(QStringLiteral("Не удалось сохранить вводные сообщения"), insert.lastError());
                m_database.rollback();
                return false;
            }
        }
        insert.finish();
        if (!execute(QStringLiteral("PRAGMA user_version = 1"))) {
            m_database.rollback();
            return false;
        }
    }
    if (!m_database.commit()) {
        m_error = sqlError(QStringLiteral("Не удалось завершить настройку базы"), m_database.lastError());
        m_database.rollback();
        return false;
    }
    return true;
}

bool LocalStore::ensureOpen() const
{
    if (!m_database.isOpen()) {
        m_error = QStringLiteral("Локальная база Nimbo не открыта.");
        return false;
    }
    return true;
}

QString LocalStore::errorString() const
{
    return m_error;
}

QList<ChatMessage> LocalStore::messages(const QString &channelId) const
{
    m_error.clear();
    if (!ensureOpen()) {
        return {};
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral(
            "SELECT id, channel_id, author, body, created_at, is_own FROM ("
            "SELECT id, channel_id, author, body, created_at, is_own FROM messages "
            "WHERE channel_id = :channel ORDER BY created_at DESC, id DESC LIMIT 300) "
            "ORDER BY created_at ASC, id ASC"))) {
        m_error = sqlError(QStringLiteral("Не удалось подготовить чтение истории"), query.lastError());
        return {};
    }
    query.bindValue(QStringLiteral(":channel"), channelId.trimmed());
    if (!query.exec()) {
        m_error = sqlError(QStringLiteral("Не удалось прочитать историю"), query.lastError());
        return {};
    }
    QList<ChatMessage> result;
    while (query.next()) {
        result.append(ChatMessage{
            query.value(0).toLongLong(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs),
            query.value(5).toBool(),
        });
    }
    if (query.lastError().isValid()) {
        m_error = sqlError(QStringLiteral("Не удалось полностью прочитать историю"), query.lastError());
        return {};
    }
    return result;
}

bool LocalStore::addMessage(const QString &channelId, const QString &author, const QString &body)
{
    m_error.clear();
    if (!ensureOpen()) {
        return false;
    }
    const auto cleanBody = body.trimmed();
    const auto cleanChannel = channelId.trimmed();
    const auto cleanAuthor = author.trimmed();
    if (cleanBody.isEmpty()) {
        m_error = QStringLiteral("Сообщение не может быть пустым.");
        return false;
    }
    if (cleanBody.size() > maxBodyLength) {
        m_error = QStringLiteral("Сообщение не может быть длиннее 4000 символов.");
        return false;
    }
    if (cleanChannel.isEmpty()) {
        m_error = QStringLiteral("Выберите канал для сообщения.");
        return false;
    }
    if (!m_database.transaction()) {
        m_error = sqlError(QStringLiteral("Не удалось начать сохранение сообщения"), m_database.lastError());
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO messages(channel_id, author, body, created_at, is_own) "
            "VALUES(:channel, :author, :body, :created, 1)"))) {
        m_error = sqlError(QStringLiteral("Не удалось подготовить сообщение"), query.lastError());
        m_database.rollback();
        return false;
    }
    query.bindValue(QStringLiteral(":channel"), cleanChannel);
    query.bindValue(QStringLiteral(":author"), cleanAuthor.isEmpty() ? QStringLiteral("Вы") : cleanAuthor);
    query.bindValue(QStringLiteral(":body"), cleanBody);
    query.bindValue(QStringLiteral(":created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        m_error = sqlError(QStringLiteral("Не удалось сохранить сообщение"), query.lastError());
        m_database.rollback();
        return false;
    }
    query.finish();
    if (!m_database.commit()) {
        m_error = sqlError(QStringLiteral("Не удалось завершить сохранение сообщения"), m_database.lastError());
        m_database.rollback();
        return false;
    }
    return true;
}

QString LocalStore::profileName() const
{
    m_error.clear();
    const auto defaultName = QStringLiteral("Вы");
    if (!ensureOpen()) {
        return defaultName;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = :key"))) {
        m_error = sqlError(QStringLiteral("Не удалось подготовить чтение профиля"), query.lastError());
        return defaultName;
    }
    query.bindValue(QStringLiteral(":key"), QStringLiteral("profile_name"));
    if (!query.exec()) {
        m_error = sqlError(QStringLiteral("Не удалось прочитать профиль"), query.lastError());
        return defaultName;
    }
    if (!query.next()) {
        if (query.lastError().isValid()) {
            m_error = sqlError(QStringLiteral("Не удалось прочитать профиль"), query.lastError());
        }
        return defaultName;
    }
    const auto name = query.value(0).toString().trimmed();
    return name.isEmpty() ? defaultName : name;
}

void LocalStore::setProfileName(const QString &name)
{
    m_error.clear();
    if (!ensureOpen()) {
        return;
    }
    if (!m_database.transaction()) {
        m_error = sqlError(QStringLiteral("Не удалось начать сохранение профиля"), m_database.lastError());
        return;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO settings(key, value) VALUES(:key, :value) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value"))) {
        m_error = sqlError(QStringLiteral("Не удалось подготовить профиль"), query.lastError());
        m_database.rollback();
        return;
    }
    const auto cleanName = name.trimmed();
    query.bindValue(QStringLiteral(":key"), QStringLiteral("profile_name"));
    query.bindValue(QStringLiteral(":value"), cleanName.isEmpty() ? QStringLiteral("Вы") : cleanName);
    if (!query.exec()) {
        m_error = sqlError(QStringLiteral("Не удалось сохранить профиль"), query.lastError());
        m_database.rollback();
        return;
    }
    query.finish();
    if (!m_database.commit()) {
        m_error = sqlError(QStringLiteral("Не удалось завершить сохранение профиля"), m_database.lastError());
        m_database.rollback();
    }
}

QString LocalStore::databasePath() const
{
    return m_databasePath;
}
