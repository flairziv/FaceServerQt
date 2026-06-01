#ifndef USERROUTES_H
#define USERROUTES_H

#include "httplib.h"
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "LoginRateLimiter.h"

class UserRoutes
{
public:
    UserRoutes(DatabaseManager &db, FaceRecognizer &recognizer, LoginRateLimiter &rateLimiter);

    // 注册所有用户相关路由
    void registerRoutes(httplib::Server &svr);

private:
    DatabaseManager &m_db;
    FaceRecognizer &m_recognizer;
    LoginRateLimiter &m_rateLimiter;

    // 路由处理器 - 用户管理
    void handleGetUserInfo(const httplib::Request &req, httplib::Response &res);
    void handleGetUserList(const httplib::Request &req, httplib::Response &res);
    void handleDeleteUser(const httplib::Request &req, httplib::Response &res);
    void handleGetUserCount(const httplib::Request &req, httplib::Response &res);

    // 路由处理器 - 用户更新
    void handleUpdatePassword(const httplib::Request &req, httplib::Response &res);
    void handleUpdateFace(const httplib::Request &req, httplib::Response &res);
    void handleVerifyFace(const httplib::Request &req, httplib::Response &res);
};

#endif // USERROUTES_H
