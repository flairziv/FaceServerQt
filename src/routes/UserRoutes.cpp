#include "UserRoutes.h"
#include "AuthMiddleware.h"
#include "PasswordUtils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// 新密码最小长度
static constexpr int kMinPasswordLength = 8;

UserRoutes::UserRoutes(DatabaseManager &db, FaceRecognizer &recognizer, LoginRateLimiter &rateLimiter)
    : m_db(db), m_recognizer(recognizer), m_rateLimiter(rateLimiter)
{
}

void UserRoutes::registerRoutes(httplib::Server &svr)
{
    // 用户管理类 API
    svr.Get("/api/user/info", [this](const auto &req, auto &res) {
        handleGetUserInfo(req, res);
    });

    svr.Get("/api/user/list", [this](const auto &req, auto &res) {
        handleGetUserList(req, res);
    });

    svr.Delete("/api/user/(.*)", [this](const auto &req, auto &res) {
        handleDeleteUser(req, res);
    });

    svr.Get("/api/user/count", [this](const auto &req, auto &res) {
        handleGetUserCount(req, res);
    });

    // 用户更新类 API
    svr.Put("/api/user/password", [this](const auto &req, auto &res) {
        handleUpdatePassword(req, res);
    });

    svr.Put("/api/user/face", [this](const auto &req, auto &res) {
        handleUpdateFace(req, res);
    });

    svr.Post("/api/user/verify-face", [this](const auto &req, auto &res) {
        handleVerifyFace(req, res);
    });
}

void UserRoutes::handleGetUserInfo(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到获取用户信息请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    QVariantMap userInfo = m_db.getUserInfo(username);

    if (userInfo.isEmpty()) {
        response["success"] = false;
        response["message"] = "用户不存在";
        res.status = 404;
    } else {
        response["success"] = true;
        response["username"] = userInfo["username"].toString();
        response["created_at"] = userInfo["created_at"].toString();
        response["last_login"] = userInfo["last_login"].toString();
        response["hasFace"] = !m_db.getUserDescriptor(username).isEmpty();
        response["hasPassword"] = !m_db.getUserPassword(username).isEmpty();
    }

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleGetUserList(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到获取用户列表请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    QVector<QVariantMap> users = m_db.getAllUsers();
    QJsonArray userArray;

    for (const auto &user : users) {
        QJsonObject userObj;
        userObj["username"] = user["username"].toString();
        userObj["created_at"] = user["created_at"].toString();
        userObj["last_login"] = user["last_login"].toString();
        userObj["hasFace"] = user["has_face"].toBool();
        userObj["hasPassword"] = user["has_password"].toBool();
        userArray.append(userObj);
    }

    response["success"] = true;
    response["count"] = userArray.size();
    response["users"] = userArray;

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleDeleteUser(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到删除用户请求";

    QString currentUser;
    if (!AuthMiddleware::extractAndVerifyToken(req, currentUser, res)) {
        return;
    }

    QString targetUsername = QString::fromStdString(req.matches[1]);
    QJsonObject response;

    // 不允许删除自己
    if (targetUsername == currentUser) {
        response["success"] = false;
        response["message"] = "不能删除当前登录的用户";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    if (!m_db.userExists(targetUsername)) {
        response["success"] = false;
        response["message"] = "用户不存在";
        res.status = 404;
    } else if (m_db.deleteUser(targetUsername)) {
        response["success"] = true;
        response["message"] = "用户删除成功";
        qInfo() << "✅ 用户" << targetUsername << "已被" << currentUser << "删除";
    } else {
        response["success"] = false;
        response["message"] = "删除失败,请稍后重试";
        res.status = 500;
    }

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleGetUserCount(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到获取用户数量请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    QJsonObject response;
    response["success"] = true;
    response["count"] = m_db.getUserCount();

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleUpdatePassword(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到修改密码请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString oldPassword = bodyJson["oldPassword"].toString();
    QString newPassword = bodyJson["newPassword"].toString();

    QJsonObject response;

    if (oldPassword.isEmpty() || newPassword.isEmpty()) {
        response["success"] = false;
        response["message"] = "旧密码和新密码不能为空";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 改密失败限流(按 IP+用户名,key 前缀与登录隔离),防止借合法 token 暴力试旧密码
    const QString rlKey = QStringLiteral("pwd|")
        + QString::fromStdString(req.remote_addr) + "|" + username;
    int waitSecs = m_rateLimiter.secondsUntilAllowed(rlKey);
    if (waitSecs > 0) {
        response["success"] = false;
        response["message"] = QString("尝试次数过多,请 %1 秒后再试").arg(waitSecs);
        res.status = 429;
        res.set_header("Retry-After", std::to_string(waitSecs));
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 新密码强度: 最小长度
    if (newPassword.length() < kMinPasswordLength) {
        response["success"] = false;
        response["message"] = QString("新密码长度不能少于 %1 位").arg(kMinPasswordLength);
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 新密码不能与旧密码相同
    if (newPassword == oldPassword) {
        response["success"] = false;
        response["message"] = "新密码不能与旧密码相同";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 验证旧密码
    QString storedPassword = m_db.getUserPassword(username);
    if (!PasswordUtils::verifyPassword(oldPassword, storedPassword)) {
        m_rateLimiter.recordFailure(rlKey);
        qWarning() << "✗ 用户" << username << "改密时旧密码验证失败";
        response["success"] = false;
        response["message"] = "旧密码错误";
        res.status = 401;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 旧密码正确,清空该 key 的失败计数
    m_rateLimiter.recordSuccess(rlKey);

    // 更新密码(argon2id)
    QString newPasswordHash = PasswordUtils::hashPassword(newPassword);
    if (newPasswordHash.isEmpty()) {
        response["success"] = false;
        response["message"] = "密码修改失败,请稍后重试";
        res.status = 500;
    } else if (m_db.updateUserPassword(username, newPasswordHash)) {
        response["success"] = true;
        response["message"] = "密码修改成功";
        qInfo() << "✅ 用户" << username << "修改密码成功";
    } else {
        response["success"] = false;
        response["message"] = "密码修改失败,请稍后重试";
        res.status = 500;
    }

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleUpdateFace(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到更新人脸请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString image = bodyJson["image"].toString();

    QJsonObject response;

    if (image.isEmpty()) {
        response["success"] = false;
        response["message"] = "人脸图像不能为空";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 提取人脸特征
    QVector<float> descriptor = m_recognizer.extractDescriptorFromBase64(image);
    if (descriptor.isEmpty()) {
        response["success"] = false;
        response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 更新人脸特征
    if (m_db.updateUserDescriptor(username, descriptor)) {
        response["success"] = true;
        response["message"] = "人脸信息更新成功";
        qInfo() << "✅ 用户" << username << "更新人脸信息成功";
    } else {
        response["success"] = false;
        response["message"] = "人脸信息更新失败,请稍后重试";
        res.status = 500;
    }

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void UserRoutes::handleVerifyFace(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到人脸验证请求";

    QString username;
    if (!AuthMiddleware::extractAndVerifyToken(req, username, res)) {
        return;
    }

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString image = bodyJson["image"].toString();

    QJsonObject response;

    if (image.isEmpty()) {
        response["success"] = false;
        response["message"] = "人脸图像不能为空";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 提取人脸特征
    QVector<float> descriptor = m_recognizer.extractDescriptorFromBase64(image);
    if (descriptor.isEmpty()) {
        response["success"] = false;
        response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 获取存储的人脸特征
    QVector<float> storedDescriptor = m_db.getUserDescriptor(username);
    if (storedDescriptor.isEmpty()) {
        response["success"] = false;
        response["message"] = "该账号未录入人脸信息";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 比对人脸
    double distance = FaceRecognizer::computeDistance(descriptor, storedDescriptor);
    bool verified = distance < FaceRecognizer::DEFAULT_THRESHOLD;

    response["success"] = verified;
    response["message"] = verified ? "人脸验证通过" : "人脸验证失败";
    response["distance"] = distance;
    response["threshold"] = FaceRecognizer::DEFAULT_THRESHOLD;

    qInfo() << (verified ? "✓" : "✗") << "用户" << username << "人脸验证"
            << (verified ? "通过" : "失败") << "(距离:" << distance << ")";

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}
