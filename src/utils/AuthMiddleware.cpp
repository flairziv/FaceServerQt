#include "AuthMiddleware.h"
#include "JwtHelper.h"
#include <QJsonDocument>
#include <QJsonObject>

bool AuthMiddleware::extractAndVerifyToken(const httplib::Request &req,
                                           QString &username,
                                           httplib::Response &res)
{
    // 获取 Authorization 头
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty()) {
        sendUnauthorized(res, "缺少认证token");
        return false;
    }

    // 提取 Bearer token
    QString authStr = QString::fromStdString(authHeader);
    if (!authStr.startsWith("Bearer ")) {
        sendUnauthorized(res, "无效的认证格式,请使用: Bearer <token>");
        return false;
    }

    QString token = authStr.mid(7); // 移除 "Bearer " 前缀

    // 验证 token
    if (!JwtHelper::verifyToken(token, username)) {
        sendUnauthorized(res, "token无效或已过期");
        return false;
    }

    return true;
}

void AuthMiddleware::sendUnauthorized(httplib::Response &res, const QString &message)
{
    QJsonObject response;
    response["success"] = false;
    response["message"] = message;
    res.status = 401;
    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}
