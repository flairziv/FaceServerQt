#ifndef JWTHELPER_H
#define JWTHELPER_H

#include <QString>

class JwtHelper
{
public:
    // 从环境变量 JWT_SECRET 读取并校验密钥,必须在 generateToken/verifyToken 之前调用一次
    // 返回 false 表示环境变量未设置或长度不足 32 字节(HS256 推荐最小长度)
    static bool initialize();

    // 生成 JWT Token
    static QString generateToken(const QString &username, int expiresInHours = 24);

    // 验证 Token 并提取用户名
    static bool verifyToken(const QString &token, QString &username);

private:
    static QString s_secretKey;
};

#endif // JWTHELPER_H
