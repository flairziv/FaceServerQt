#include "DatabaseManager.h"
#include <QDebug>
#include <QDataStream>
#include <QDateTime>
#include <QThread>
#include <QMutexLocker>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
}

DatabaseManager::~DatabaseManager()
{
    // 连接是各工作线程惰性创建的线程本地连接;不在此(可能是别的线程)调用
    // removeDatabase —— 跨线程移除不安全。进程退出时由 OS 回收。
}

QSqlDatabase DatabaseManager::threadConnection()
{
    // 每个线程一条独立连接,连接名按 thread-id 唯一,符合 Qt「连接只在创建它的线程使用」的规则。
    const QString name = QStringLiteral("face_db_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    // 缓存命中(锁外):该名字只会被其所属线程访问,无 TOCTOU;给此路径加锁反会串行化所有查询。
    // database() 默认 open=true,连接若被丢弃会自动重连。
    if (QSqlDatabase::contains(name)) {
        return QSqlDatabase::database(name);
    }

    // 首次创建:仅此分支加锁(每线程一次),纯防御性。
    QMutexLocker locker(&m_connectMutex);
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", name);
    db.setHostName(m_host);
    db.setPort(m_port);
    db.setDatabaseName(m_dbName);
    db.setUserName(m_user);
    db.setPassword(m_password);
    if (!db.open()) {
        qCritical() << "❌ 线程数据库连接失败:" << db.lastError().text();
    }
    return db;
}

bool DatabaseManager::initialize(const QString &host, int port, const QString &dbName,
                                const QString &user, const QString &password)
{
    // 保存连接参数,后续每个工作线程据此惰性建立自己的连接
    m_host = host;
    m_port = port;
    m_dbName = dbName;
    m_user = user;
    m_password = password;

    // 主线程连接(并触发驱动加载),随后建表
    QSqlDatabase db = threadConnection();
    if (!db.isOpen()) {
        qCritical() << "❌ 数据库连接失败";
        return false;
    }

    qInfo() << "✅ 数据库连接成功";
    return createTables();
}

bool DatabaseManager::createTables()
{
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);

    QString createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(128) UNIQUE NOT NULL,
            face_descriptor LONGBLOB DEFAULT NULL COMMENT '128维人脸特征向量',
            password_hash VARCHAR(255) DEFAULT NULL COMMENT 'argon2id 加盐哈希(兼容旧版SHA-256)',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_login TIMESTAMP NULL DEFAULT NULL,
            INDEX idx_username (username)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
    )";

    if (!query.exec(createTableSQL)) {
        qCritical() << "❌ 创建表失败:" << query.lastError().text();
        return false;
    }

    // 迁移: argon2id 哈希串(~100字符)比旧版 SHA-256(64字符)更长,需扩展列宽。
    // MySQL MODIFY COLUMN 幂等,可安全重复执行;失败仅告警,不中断启动。
    QSqlQuery migrate(db);
    if (!migrate.exec("ALTER TABLE users MODIFY COLUMN password_hash "
                      "VARCHAR(255) DEFAULT NULL COMMENT 'argon2id 加盐哈希(兼容旧版SHA-256)'")) {
        qWarning() << "⚠ password_hash 列宽迁移失败(可能已是新版):" << migrate.lastError().text();
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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

// 插入人脸特征和密码哈希
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

    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
    
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query("SELECT username, created_at, last_login, "
                   "(face_descriptor IS NOT NULL) as has_face, "
                   "(password_hash IS NOT NULL) as has_password "
                   "FROM users ORDER BY created_at DESC", db);

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

QVector<QPair<QString, QVector<float>>> DatabaseManager::getAllUserDescriptors()
{
    QVector<QPair<QString, QVector<float>>> results;
    QSqlDatabase db = threadConnection();
    QSqlQuery query("SELECT username, face_descriptor FROM users "
                    "WHERE face_descriptor IS NOT NULL", db);

    while (query.next()) {
        QByteArray blob = query.value(1).toByteArray();
        if (blob.isEmpty()) continue;
        results.append(qMakePair(query.value(0).toString(), blobToDescriptor(blob)));
    }

    return results;
}

bool DatabaseManager::deleteUser(const QString &username)
{
    QSqlDatabase db = threadConnection();
    QSqlQuery query(db);
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
    QSqlDatabase db = threadConnection();
    QSqlQuery query("SELECT COUNT(*) FROM users", db);
    
    if (query.next()) {
        return query.value(0).toInt();
    }
    
    return 0;
}
