#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QVariantMap>
#include <QPair>
#include <QMutex>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    // 初始化数据库连接
    bool initialize(const QString &host, int port, const QString &dbName,
                   const QString &user, const QString &password);

    // 用户操作
    bool userExists(const QString &username);
    
    // 插入用户
    bool insertUser(const QString &username, const QVector<float> &faceDescriptor, const QString &passwordHash);
    
    // 查询用户数据
    QVector<float> getUserDescriptor(const QString &username);
    QString getUserPassword(const QString &username);
    QVariantMap getUserInfo(const QString &username);
    QVector<QVariantMap> getAllUsers();

    // 一次查询返回所有已录入人脸的用户及其 128-d 特征向量
    // 供 /api/face/search 等需要全表扫描的场景使用,避免 N+1
    QVector<QPair<QString, QVector<float>>> getAllUserDescriptors();
    
    // 更新用户数据
    bool updateLastLogin(const QString &username);
    bool updateUserPassword(const QString &username, const QString &newPasswordHash);
    bool updateUserDescriptor(const QString &username, const QVector<float> &newDescriptor);
    
    // 删除用户
    bool deleteUser(const QString &username);
    
    // 统计
    int getUserCount();

private:
    bool createTables();
    QByteArray descriptorToBlob(const QVector<float> &descriptor);
    QVector<float> blobToDescriptor(const QByteArray &blob);

    // 返回当前线程专属的数据库连接(线程本地)。
    // Qt 规定:一个连接只能在创建它的线程里使用。httplib 用线程池并发分发请求,
    // 因此每个工作线程按自身 thread-id 惰性创建并复用一条独立连接。
    QSqlDatabase threadConnection();

    // 连接参数(initialize 时保存,threadConnection 据此建连)
    QString m_host;
    int m_port = 3306;
    QString m_dbName;
    QString m_user;
    QString m_password;

    // 仅保护「首次为某线程创建连接」这一分支;缓存命中路径无需加锁。
    QMutex m_connectMutex;
};

#endif // DATABASEMANAGER_H
