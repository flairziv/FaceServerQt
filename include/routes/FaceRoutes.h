#ifndef FACEROUTES_H
#define FACEROUTES_H

#include "httplib.h"
#include "DatabaseManager.h"
#include "FaceRecognizer.h"

class FaceRoutes
{
public:
    FaceRoutes(DatabaseManager &db, FaceRecognizer &recognizer);

    // 注册所有人脸相关路由
    void registerRoutes(httplib::Server &svr);

private:
    DatabaseManager &m_db;
    FaceRecognizer &m_recognizer;

    // 路由处理器
    void handleRegister(const httplib::Request &req, httplib::Response &res);
    void handleLogin(const httplib::Request &req, httplib::Response &res);
    void handleDetect(const httplib::Request &req, httplib::Response &res);
    void handleCompare(const httplib::Request &req, httplib::Response &res);
    void handleSearch(const httplib::Request &req, httplib::Response &res);
};

#endif // FACEROUTES_H
