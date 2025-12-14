#include "DatabaseManager.h"
#include <QDebug>
#include <QDataStream>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
}

DatabaseManager::~DatabaseManager()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::initialize(const QString &host, int port, const QString &dbName,
                                const QString &user, const QString &password)
{
    // 向 QSqlDatabase 类添加一个新的数据库连接
    m_db = QSqlDatabase::addDatabase("QMYSQL");  // "QMYSQL" 是指 MySQL 数据库的驱动程序
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(dbName);
    m_db.setUserName(user);
    m_db.setPassword(password);

    if (!m_db.open()) {
        qCritical() << "❌ 数据库连接失败:" << m_db.lastError().text();
        return false;
    }

    qInfo() << "✅ 数据库连接成功";
    return createTables();
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);
    
    QString createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(128) UNIQUE NOT NULL,
            face_descriptor LONGBLOB NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            INDEX idx_username (username)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )";

    if (!query.exec(createTableSQL)) {
        qCritical() << "❌ 创建表失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 数据表初始化成功";
    return true;
}

QByteArray DatabaseManager::descriptorToBlob(const QVector<float> &descriptor)
{
    QByteArray byteArray;
    QDataStream stream(&byteArray, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << descriptor;
    return byteArray;
}

QVector<float> DatabaseManager::blobToDescriptor(const QByteArray &blob)
{
    QVector<float> descriptor;
    QDataStream stream(blob);
    stream.setVersion(QDataStream::Qt_5_12);
    stream >> descriptor;
    return descriptor;
}

bool DatabaseManager::userExists(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM users WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "查询用户失败:" << query.lastError().text();
        return false;
    }

    if (query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool DatabaseManager::insertUser(const QString &username, const QVector<float> &faceDescriptor)
{
    if (userExists(username)) {
        qWarning() << "用户已存在:" << username;
        return false;
    }

    QSqlQuery query(m_db);
    // 准备 SQL 语句，其中 VALUES (:username, :descriptor) ":username" 和 ":descriptor" 是 SQL 语句中的占位符
    query.prepare("INSERT INTO users (username, face_descriptor) VALUES (:username, :descriptor)");
    // 将变量 username 绑定到 :username 占位符
    query.bindValue(":username", username);
    // 将变量 descriptor 绑定到 :descriptor 占位符
    query.bindValue(":descriptor", descriptorToBlob(faceDescriptor));

    if (!query.exec()) {
        qCritical() << "插入用户失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 用户注册成功:" << username;
    return true;
}

QVector<float> DatabaseManager::getUserDescriptor(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT face_descriptor FROM users WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "查询失败:" << query.lastError().text();
        return {};
    }

    if (query.next()) {
        QByteArray blob = query.value(0).toByteArray();
        return blobToDescriptor(blob);
    }

    return {};
}

QVector<QVariantMap> DatabaseManager::getAllUsers()
{
    QVector<QVariantMap> users;
    QSqlQuery query("SELECT username FROM users", m_db);

    while (query.next()) {
        QVariantMap user;
        user["username"] = query.value(0).toString();
        users.append(user);
    }

    return users;
}

bool DatabaseManager::deleteUser(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE username = :username");
    query.bindValue(":username", username);
    return query.exec();
}
