#ifndef JWTHELPER_H
#define JWTHELPER_H

#include <QString>

class JwtHelper
{
public:
    // 生成 JWT Token
    static QString generateToken(const QString &username, int expiresInHours = 24);
    
    // 验证 Token 并提取用户名
    static bool verifyToken(const QString &token, QString &username);

private:
    static const QString SECRET_KEY;
};

#endif // JWTHELPER_H
