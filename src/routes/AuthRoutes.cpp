#include "AuthRoutes.h"
#include "AuthMiddleware.h"
#include "JwtHelper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

AuthRoutes::AuthRoutes()
{
}

void AuthRoutes::registerRoutes(httplib::Server &svr)
{
    svr.Post("/api/auth/verify", [this](const auto &req, auto &res) {
        handleVerifyToken(req, res);
    });

    svr.Post("/api/auth/refresh", [this](const auto &req, auto &res) {
        handleRefreshToken(req, res);
    });

    svr.Post("/api/auth/logout", [this](const auto &req, auto &res) {
        handleLogout(req, res);
    });
}

void AuthRoutes::handleVerifyToken(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到token验证请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    response["success"] = true;
    response["message"] = "token有效";
    response["username"] = username;

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void AuthRoutes::handleRefreshToken(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到token刷新请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    // 生成新token
    QString newToken = JwtHelper::generateToken(username);

    QJsonObject response;
    response["success"] = true;
    response["message"] = "token刷新成功";
    response["token"] = newToken;
    response["username"] = username;

    qInfo() << "✅ 用户" << username << "刷新token成功";

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void AuthRoutes::handleLogout(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到登出请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    response["success"] = true;
    response["message"] = "登出成功";

    qInfo() << "👋 用户" << username << "登出";

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}
