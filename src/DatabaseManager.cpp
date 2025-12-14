#include "DatabaseManager.h"
#include <QDebug>
#include <QDataStream>
#include <QDateTime>

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
    
    // 更新表结构,添加 password_hash 和 last_login 字段
    QString createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(128) UNIQUE NOT NULL,
            face_descriptor LONGBLOB DEFAULT NULL COMMENT '128维人脸特征向量',
            password_hash VARCHAR(64) DEFAULT NULL COMMENT '密码SHA256哈希',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_login TIMESTAMP NULL DEFAULT NULL,
            INDEX idx_username (username)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
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

// 重载方法1: 只插入人脸特征(兼容旧代码)
bool DatabaseManager::insertUser(const QString &username, const QVector<float> &faceDescriptor)
{
    return insertUser(username, faceDescriptor, QString());
}

// 重载方法2: 插入人脸特征和密码哈希
bool DatabaseManager::insertUser(const QString &username, const QVector<float> &faceDescriptor, 
                                const QString &passwordHash)
{
    if (userExists(username)) {
        qWarning() << "用户已存在:" << username;
        return false;
    }

    // 检查至少提供一种认证方式
    if (faceDescriptor.isEmpty() && passwordHash.isEmpty()) {
        qWarning() << "必须提供人脸特征或密码";
        return false;
    }

    QSqlQuery query(m_db);
    
    // 根据提供的数据动态构建 SQL
    QString sql;
    if (!faceDescriptor.isEmpty() && !passwordHash.isEmpty()) {
        // 同时提供人脸和密码
        sql = "INSERT INTO users (username, face_descriptor, password_hash) "
              "VALUES (:username, :descriptor, :password)";
    } else if (!faceDescriptor.isEmpty()) {
        // 仅提供人脸
        sql = "INSERT INTO users (username, face_descriptor) "
              "VALUES (:username, :descriptor)";
    } else {
        // 仅提供密码
        sql = "INSERT INTO users (username, password_hash) "
              "VALUES (:username, :password)";
    }

    query.prepare(sql);
    query.bindValue(":username", username);
    
    if (!faceDescriptor.isEmpty()) {
        query.bindValue(":descriptor", descriptorToBlob(faceDescriptor));
    }
    if (!passwordHash.isEmpty()) {
        query.bindValue(":password", passwordHash);
    }

    if (!query.exec()) {
        qCritical() << "插入用户失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 用户注册成功:" << username 
            << (faceDescriptor.isEmpty() ? "" : "[人脸]")
            << (passwordHash.isEmpty() ? "" : "[密码]");
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
        if (blob.isEmpty()) {
            return {};  // 用户没有录入人脸
        }
        return blobToDescriptor(blob);
    }

    return {};
}

QString DatabaseManager::getUserPassword(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT password_hash FROM users WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "查询密码失败:" << query.lastError().text();
        return QString();
    }

    if (query.next()) {
        return query.value(0).toString();
    }

    return QString();
}

bool DatabaseManager::updateLastLogin(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET last_login = :time WHERE username = :username");
    query.bindValue(":time", QDateTime::currentDateTime());
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "更新登录时间失败:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::updateUserPassword(const QString &username, const QString &newPasswordHash)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET password_hash = :password WHERE username = :username");
    query.bindValue(":password", newPasswordHash);
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "更新密码失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 用户" << username << "密码已更新";
    return true;
}

bool DatabaseManager::updateUserDescriptor(const QString &username, const QVector<float> &newDescriptor)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET face_descriptor = :descriptor WHERE username = :username");
    query.bindValue(":descriptor", descriptorToBlob(newDescriptor));
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "更新人脸特征失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 用户" << username << "人脸特征已更新";
    return true;
}

QVariantMap DatabaseManager::getUserInfo(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT id, username, created_at, last_login, "
                 "(face_descriptor IS NOT NULL) as has_face, "
                 "(password_hash IS NOT NULL) as has_password "
                 "FROM users WHERE username = :username");
    query.bindValue(":username", username);

    if (!query.exec()) {
        qWarning() << "查询用户信息失败:" << query.lastError().text();
        return {};
    }

    if (query.next()) {
        QVariantMap info;
        info["id"] = query.value(0).toInt();
        info["username"] = query.value(1).toString();
        info["created_at"] = query.value(2).toString();
        info["last_login"] = query.value(3).toString();
        info["has_face"] = query.value(4).toBool();
        info["has_password"] = query.value(5).toBool();
        return info;
    }

    return {};
}

QVector<QVariantMap> DatabaseManager::getAllUsers()
{
    QVector<QVariantMap> users;
    QSqlQuery query("SELECT username, created_at, last_login, "
                   "(face_descriptor IS NOT NULL) as has_face, "
                   "(password_hash IS NOT NULL) as has_password "
                   "FROM users ORDER BY created_at DESC", m_db);

    while (query.next()) {
        QVariantMap user;
        user["username"] = query.value(0).toString();
        user["created_at"] = query.value(1).toString();
        user["last_login"] = query.value(2).toString();
        user["has_face"] = query.value(3).toBool();
        user["has_password"] = query.value(4).toBool();
        users.append(user);
    }

    return users;
}

bool DatabaseManager::deleteUser(const QString &username)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE username = :username");
    query.bindValue(":username", username);
    
    if (!query.exec()) {
        qWarning() << "删除用户失败:" << query.lastError().text();
        return false;
    }

    qInfo() << "✅ 用户" << username << "已删除";
    return true;
}

int DatabaseManager::getUserCount()
{
    QSqlQuery query("SELECT COUNT(*) FROM users", m_db);
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}
