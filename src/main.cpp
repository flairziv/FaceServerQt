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
