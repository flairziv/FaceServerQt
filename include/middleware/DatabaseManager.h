#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QVariantMap>
#include <QPair>

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

    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H
