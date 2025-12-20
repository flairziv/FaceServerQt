#include <QCoreApplication>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QDateTime>
#include <QCryptographicHash>
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "JwtHelper.h"
#include "httplib.h"

// 状态码:2xx——成功、3xx——重定向、4xx——客户端错误、5xx——服务器错误

// 辅助函数:密码哈希
QString hashPassword(const QString &password)
{
    return QString(QCryptographicHash::hash(password.toUtf8(),
                                            QCryptographicHash::Sha256)
                       .toHex());
}

// 辅助函数:从请求头提取并验证JWT token
bool extractAndVerifyToken(const httplib::Request &req, QString &username, httplib::Response &res)
{
    // 获取 Authorization 头
    auto authHeader = req.get_header_value("Authorization");
    if (authHeader.empty()) {
        QJsonObject response;
        response["success"] = false;
        response["message"] = "缺少认证token";
        res.status = 401;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return false;
    }

    // 提取 Bearer token
    QString authStr = QString::fromStdString(authHeader);
    if (!authStr.startsWith("Bearer ")) {
        QJsonObject response;
        response["success"] = false;
        response["message"] = "无效的认证格式,请使用: Bearer <token>";
        res.status = 401;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return false;
    }

    QString token = authStr.mid(7); // 移除 "Bearer " 前缀

    // 验证 token
    if (!JwtHelper::verifyToken(token, username)) {
        QJsonObject response;
        response["success"] = false;
        response["message"] = "token无效或已过期";
        res.status = 401;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "========================================";
    qInfo() << "  人脸识别服务器 - C++ Qt + dlib 版本";
    qInfo() << "========================================";

    // 初始化数据库
    DatabaseManager db;
    if (!db.initialize("127.0.0.1", 3306, "face_recognition_db", "faceuser", "FacePass2025"))
    {
        qCritical() << "数据库初始化失败,退出";
        return -1;
    }

    // 初始化人脸识别器
    FaceRecognizer recognizer;
    if (!recognizer.loadModels(
            "models/shape_predictor_68_face_landmarks.dat",
            "models/dlib_face_recognition_resnet_model_v1.dat"))
    {
        qCritical() << "模型加载失败,退出";
        return -1;
    }

    // 创建 HTTP 服务器
    httplib::Server svr;

    // 设置 CORS(允许前端跨域访问)
    svr.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res)
                                {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        if (req.method == "OPTIONS") {
            res.status = 200;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled; });

    // ========== API: 健康检查 ==========
    svr.Get("/api/health", [](const httplib::Request &, httplib::Response &res)
            {
        QJsonObject json;
        json["status"] = "ok";
        json["message"] = "服务运行正常";
        json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        res.set_content(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString(), 
                       "application/json"); });

    // ========== API: 用户注册 ==========
    svr.Post("/api/face/register", [&](const httplib::Request &req, httplib::Response &res)
             {
        qInfo() << "收到注册请求";
        
        auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        QString username = bodyJson["username"].toString();
        QString password = bodyJson["password"].toString();
        QString image = bodyJson["image"].toString();

        QJsonObject response;

        // 验证输入
        if (username.isEmpty()) {
            response["success"] = false;
            response["message"] = "用户名不能为空";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        if (password.isEmpty() && image.isEmpty()) {
            response["success"] = false;
            response["message"] = "密码和人脸信息至少需要提供一个";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 检查用户是否已存在
        if (db.userExists(username)) {
            response["success"] = false;
            response["message"] = "用户名已存在";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 处理人脸特征
        QVector<float> descriptor;
        if (!image.isEmpty()) {
            descriptor = recognizer.extractDescriptorFromBase64(image);
            if (descriptor.isEmpty()) {
                response["success"] = false;
                response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
                res.status = 400;
                res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                               "application/json");
                return;
            }
        }

        // 密码哈希处理
        QString hashedPassword;
        if (!password.isEmpty()) {
            hashedPassword = hashPassword(password);
        }

        // 保存到数据库(需要修改DatabaseManager支持密码字段)
        if (!db.insertUser(username, descriptor, hashedPassword)) {
            response["success"] = false;
            response["message"] = "注册失败,请稍后重试";
            res.status = 500;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        response["success"] = true;
        response["message"] = "注册成功";
        response["username"] = username;
        response["hasFace"] = !descriptor.isEmpty();
        response["hasPassword"] = !password.isEmpty();
        
        qInfo() << "✅ 用户" << username << "注册成功";
        
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // ========== API: 用户登录 ==========
    svr.Post("/api/face/login", [&](const httplib::Request &req, httplib::Response &res)
             {
        qInfo() << "收到登录请求";
        
        auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        QString username = bodyJson["username"].toString();
        QString password = bodyJson["password"].toString();
        QString image = bodyJson["image"].toString();

        QJsonObject response;

        // 强制要求所有字段
        if (username.isEmpty() || password.isEmpty() || image.isEmpty()) {
            response["success"] = false;
            response["message"] = "请提供完整的认证信息(账号+密码+人脸)";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        // 第一步: 验证用户是否存在
        if (!db.userExists(username)) {
            response["success"] = false;
            response["message"] = "用户不存在";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        // 第二步: 验证密码
        QString hashedPassword = hashPassword(password);
        QString storedPassword = db.getUserPassword(username);
        
        if (storedPassword.isEmpty()) {
            response["success"] = false;
            response["message"] = "该账号未设置密码,请联系管理员";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        if (storedPassword != hashedPassword) {
            qWarning() << "✗ 用户" << username << "密码验证失败";
            response["success"] = false;
            response["message"] = "密码错误";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        qInfo() << "✓ 用户" << username << "密码验证通过";

        // 第三步: 验证人脸
        QVector<float> descriptor = recognizer.extractDescriptorFromBase64(image);
        
        if (descriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        QVector<float> storedDescriptor = db.getUserDescriptor(username);
        
        if (storedDescriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "该账号未录入人脸信息,请联系管理员";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        double distance = FaceRecognizer::computeDistance(descriptor, storedDescriptor);
        qInfo() << "与用户" << username << "的人脸距离:" << distance;
        
        if (distance >= 0.45) {  // 阈值: dlib 推荐 0.6,这里用 0.45 更严格
            qWarning() << "✗ 用户" << username << "人脸验证失败 (距离:" << distance << ")";
            response["success"] = false;
            response["message"] = QString("人脸识别失败,相似度不足 (距离: %1)").arg(distance, 0, 'f', 3);
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                        "application/json");
            return;
        }

        qInfo() << "✓ 用户" << username << "人脸验证通过 (距离:" << distance << ")";

        // 认证通过
        QString token = JwtHelper::generateToken(username);
        db.updateLastLogin(username);

        response["success"] = true;
        response["message"] = "认证成功";
        response["username"] = username;
        response["token"] = token;
        response["authMethod"] = "密码+人脸双重认证";
        response["faceDistance"] = distance;
        
        qInfo() << "🎉 用户" << username << "登录成功";
        
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                    "application/json"); });

    // ========== 1. 用户管理类 API ==========

    // API: 获取当前用户信息
    svr.Get("/api/user/info", [&](const httplib::Request &req, httplib::Response &res)
            {
        qInfo() << "收到获取用户信息请求";

        QString username;
        if (!extractAndVerifyToken(req, username, res)) {
            return;
        }

        QJsonObject response;
        QVariantMap userInfo = db.getUserInfo(username);

        if (userInfo.isEmpty()) {
            response["success"] = false;
            response["message"] = "用户不存在";
            res.status = 404;
        } else {
            response["success"] = true;
            response["username"] = userInfo["username"].toString();
            response["created_at"] = userInfo["created_at"].toString();
            response["last_login"] = userInfo["last_login"].toString();
            response["hasFace"] = !db.getUserDescriptor(username).isEmpty();
            response["hasPassword"] = !db.getUserPassword(username).isEmpty();
        }

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // API: 获取所有用户列表
    svr.Get("/api/user/list", [&](const httplib::Request &req, httplib::Response &res)
            {
        qInfo() << "收到获取用户列表请求";

        QString username;
        if (!extractAndVerifyToken(req, username, res)) {
            return;
        }

        QJsonObject response;
        QVector<QVariantMap> users = db.getAllUsers();
        QJsonArray userArray;

        for (const auto &user : users) {
            QJsonObject userObj;
            userObj["username"] = user["username"].toString();
            userObj["created_at"] = user["created_at"].toString();
            userObj["last_login"] = user["last_login"].toString();
            userObj["hasFace"] = !db.getUserDescriptor(user["username"].toString()).isEmpty();
            userObj["hasPassword"] = !db.getUserPassword(user["username"].toString()).isEmpty();
            userArray.append(userObj);
        }

        response["success"] = true;
        response["count"] = userArray.size();
        response["users"] = userArray;

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // API: 删除用户
    // # 删除用户 "admin"  
    // curl -X DELETE http://localhost:3000/api/user/admin \
    // -H "Authorization: Bearer <your_token>"
    // (.*) 是一个正则表达式，.* 表示匹配任意字符任意次数，() 表示捕获括号内匹配的内容
    svr.Delete("/api/user/(.*)", [&](const httplib::Request &req, httplib::Response &res)
               {
        qInfo() << "收到删除用户请求";

        QString currentUser;
        if (!extractAndVerifyToken(req, currentUser, res)) {
            return;
        }

        // req.matches 是 httplib 库提供的匹配结果数组，req.matches[0] 完整匹配的字符串，req.matches[1]第一个捕获组 (.*) 的内容
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

        if (!db.userExists(targetUsername)) {
            response["success"] = false;
            response["message"] = "用户不存在";
            res.status = 404;
        } else if (db.deleteUser(targetUsername)) {
            response["success"] = true;
            response["message"] = "用户删除成功";
            qInfo() << "✅ 用户" << targetUsername << "已被" << currentUser << "删除";
        } else {
            response["success"] = false;
            response["message"] = "删除失败,请稍后重试";
            res.status = 500;
        }

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // API: 获取用户总数统计
    svr.Get("/api/user/count", [&](const httplib::Request &req, httplib::Response &res)
            {
        qInfo() << "收到获取用户数量请求";

        QString username;
        if (!extractAndVerifyToken(req, username, res)) {
            return;
        }

        QJsonObject response;
        response["success"] = true;
        response["count"] = db.getUserCount();

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });


    // ========== 2. 用户更新类 API ==========

    // API: 修改密码
    svr.Put("/api/user/password", [&](const httplib::Request &req, httplib::Response &res)
            {
        qInfo() << "收到修改密码请求";

        QString username;
        if (!extractAndVerifyToken(req, username, res)) {
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

        // 验证旧密码
        QString storedPassword = db.getUserPassword(username);
        if (storedPassword != hashPassword(oldPassword)) {
            response["success"] = false;
            response["message"] = "旧密码错误";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 更新密码
        QString newPasswordHash = hashPassword(newPassword);
        if (db.updateUserPassword(username, newPasswordHash)) {
            response["success"] = true;
            response["message"] = "密码修改成功";
            qInfo() << "✅ 用户" << username << "修改密码成功";
        } else {
            response["success"] = false;
            response["message"] = "密码修改失败,请稍后重试";
            res.status = 500;
        }

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // API: 更新人脸信息
    svr.Put("/api/user/face", [&](const httplib::Request &req, httplib::Response &res)
            {
        qInfo() << "收到更新人脸请求";

        QString username;
        if (!extractAndVerifyToken(req, username, res)) {
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
        QVector<float> descriptor = recognizer.extractDescriptorFromBase64(image);
        if (descriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 更新人脸特征
        if (db.updateUserDescriptor(username, descriptor)) {
            response["success"] = true;
            response["message"] = "人脸信息更新成功";
            qInfo() << "✅ 用户" << username << "更新人脸信息成功";
        } else {
            response["success"] = false;
            response["message"] = "人脸信息更新失败,请稍后重试";
            res.status = 500;
        }

        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json"); });

    // 在独立线程中启动 HTTP 服务器
    QThread *serverThread = QThread::create([&]()
                                            {
        qInfo() << "🚀 HTTP 服务器启动在 http://0.0.0.0:3000";
        svr.listen("0.0.0.0", 3000); });
    serverThread->start();

    int ret = app.exec();

    svr.stop();
    serverThread->quit();
    serverThread->wait();
    delete serverThread;

    return ret;
}
