#ifndef AUTHMIDDLEWARE_H
#define AUTHMIDDLEWARE_H

#include <QString>
#include "httplib.h"

class AuthMiddleware
{
public:
    // 从请求头提取并验证JWT token
    static bool extractAndVerifyToken(const httplib::Request &req,
                                     QString &username,
                                     httplib::Response &res);

private:
    static void sendUnauthorized(httplib::Response &res, const QString &message);
};

#endif // AUTHMIDDLEWARE_H
