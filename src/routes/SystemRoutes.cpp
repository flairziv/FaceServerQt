#include "SystemRoutes.h"
#include "AuthMiddleware.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

SystemRoutes::SystemRoutes(DatabaseManager &db) : m_db(db)
{
}

void SystemRoutes::registerRoutes(httplib::Server &svr)
{
    svr.Get("/api/health", [this](const auto &req, auto &res) {
        handleHealth(req, res);
    });

    svr.Get("/api/system/stats", [this](const auto &req, auto &res) {
        handleStats(req, res);
    });
}

void SystemRoutes::handleHealth(const httplib::Request &req, httplib::Response &res)
{
    QJsonObject json;
    json["status"] = "ok";
    json["message"] = "服务运行正常";
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    res.set_content(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void SystemRoutes::handleStats(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到系统统计请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    response["success"] = true;
    response["totalUsers"] = m_db.getUserCount();
    response["serverVersion"] = "1.0.0";
    response["serverUptime"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    response["databaseConnected"] = true;
    response["modelsLoaded"] = true;

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}
