#ifndef SYSTEMROUTES_H
#define SYSTEMROUTES_H

#include "httplib.h"
#include "DatabaseManager.h"

class SystemRoutes
{
public:
    explicit SystemRoutes(DatabaseManager &db);

    // 注册所有系统相关路由
    void registerRoutes(httplib::Server &svr);

private:
    DatabaseManager &m_db;

    // 路由处理器
    void handleHealth(const httplib::Request &req, httplib::Response &res);
    void handleStats(const httplib::Request &req, httplib::Response &res);
};

#endif // SYSTEMROUTES_H
