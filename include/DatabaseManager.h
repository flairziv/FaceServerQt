#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QVariantMap>
#include <QString>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    // 初始化数据库连接
    bool initialize(const QString &host, int port, const QString &dbName,
                   const QString &user, const QString &password);
    
    // 创建表
    bool createTables();
    
    // 用户操作
    bool userExists(const QString &username);
    bool insertUser(const QString &username, const QVector<float> &faceDescriptor);
    QVector<float> getUserDescriptor(const QString &username);
    QVector<QVariantMap> getAllUsers();
    bool deleteUser(const QString &username);

private:
    QSqlDatabase m_db;
    
    // 辅助函数：将 QVector<float> 转换为 BLOB
    // 将 QVector<float> 类型的面部特征描述符序列化为二进制数据（QByteArray），方便后续存储到数据库中
    QByteArray descriptorToBlob(const QVector<float> &descriptor);
    QVector<float> blobToDescriptor(const QByteArray &blob);
};

#endif // DATABASEMANAGER_H
