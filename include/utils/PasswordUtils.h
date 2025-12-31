#ifndef PASSWORDUTILS_H
#define PASSWORDUTILS_H

#include <QString>

class PasswordUtils
{
public:
    // 密码哈希
    static QString hashPassword(const QString &password);
};

#endif // PASSWORDUTILS_H
