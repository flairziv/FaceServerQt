#ifndef AUTHROUTES_H
#define AUTHROUTES_H

#include "httplib.h"

class AuthRoutes
{
public:
    AuthRoutes();

    // 注册所有认证相关路由
    void registerRoutes(httplib::Server &svr);

private:
    // 路由处理器
    void handleVerifyToken(const httplib::Request &req, httplib::Response &res);
    void handleRefreshToken(const httplib::Request &req, httplib::Response &res);
    void handleLogout(const httplib::Request &req, httplib::Response &res);
};

#endif // AUTHROUTES_H
