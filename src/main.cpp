#include <QCoreApplication>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QDateTime>
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "JwtHelper.h"
#include "httplib.h"

// 状态码：2xx——成功、3xx——重定向、4xx——客户端错误、5xx——服务器错误

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qInfo() << "========================================";
    qInfo() << "  人脸识别服务器 - C++ Qt + dlib 版本";
    qInfo() << "========================================";

    // 初始化数据库
    DatabaseManager db;
    if (!db.initialize("127.0.0.1", 3306, "face_recognition_db", "faceuser", "FacePass2025")) {
        qCritical() << "数据库初始化失败，退出";
        return -1;
    }

    // 初始化人脸识别器
    FaceRecognizer recognizer;
    if (!recognizer.loadModels(
            "models/shape_predictor_68_face_landmarks.dat",
            "models/dlib_face_recognition_resnet_model_v1.dat")) {
        qCritical() << "模型加载失败，退出";
        return -1;
    }

    // 创建 HTTP 服务器
    httplib::Server svr;

    // 设置 CORS（允许前端跨域访问）
    svr.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        // 允许所有域名跨域访问
        res.set_header("Access-Control-Allow-Origin", "*");
        // 声明服务器支持的跨域请求方法（GET/POST/OPTIONS）
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        // 声明服务器允许的请求头（Content-Type 用于 JSON 传参，Authorization 用于 Token 鉴权）
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        // 处理「预检请求（Preflight Request）」：
        // 前端发送复杂跨域请求（如带自定义头、POST JSON）时，
        // 会先发送 OPTIONS 请求探测服务器是否允许跨域，
        // 此处直接返回 200 并标记「已处理」，避免进入后续路由匹配
        if (req.method == "OPTIONS") {
            res.status = 200;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ========== API: 健康检查 ==========
    svr.Get("/api/health", [](const httplib::Request &, httplib::Response &res) {
        QJsonObject json;
        json["status"] = "ok";
        json["message"] = "服务运行正常";
        json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        // 设置响应体和 Content-Type 为 application/json（前端可正确解析 JSON）
        res.set_content(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString(), 
                       "application/json");
    });

    // ========== API: 用户注册 ==========
    svr.Post("/api/face/register", [&](const httplib::Request &req, httplib::Response &res) {
        qInfo() << "收到注册请求";
        
        auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        QString username = bodyJson["username"].toString();
        QString image = bodyJson["image"].toString();

        QJsonObject response;

        // 验证输入
        if (username.isEmpty() || image.isEmpty()) {
            response["success"] = false;
            response["message"] = "缺少用户名或图像数据";
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

        // 提取人脸特征
        QVector<float> descriptor = recognizer.extractDescriptorFromBase64(image);
        if (descriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "未检测到人脸，请确保光线充足并正对摄像头";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 保存到数据库
        if (!db.insertUser(username, descriptor)) {
            response["success"] = false;
            response["message"] = "注册失败，请稍后重试";
            res.status = 500;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        response["success"] = true;
        response["message"] = "注册成功";
        response["username"] = username;
        
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
    });

    // ========== API: 用户登录 ==========
    svr.Post("/api/face/login", [&](const httplib::Request &req, httplib::Response &res) {
        qInfo() << "收到登录请求";
        
        auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        QString image = bodyJson["image"].toString();

        QJsonObject response;

        if (image.isEmpty()) {
            response["success"] = false;
            response["message"] = "缺少图像数据";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 提取人脸特征
        QVector<float> descriptor = recognizer.extractDescriptorFromBase64(image);
        if (descriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "未检测到人脸，请确保光线充足并正对摄像头";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 查找最匹配的用户
        QString bestMatch;
        double minDistance = 0.45;  // 阈值：dlib 推荐 0.6
        
        auto allUsers = db.getAllUsers();
        for (const auto &userMap : allUsers) {
            QString username = userMap["username"].toString();
            QVector<float> storedDescriptor = db.getUserDescriptor(username);
            
            if (storedDescriptor.isEmpty()) {
                continue;
            }

            double distance = FaceRecognizer::computeDistance(descriptor, storedDescriptor);
            qInfo() << "与用户" << username << "的距离:" << distance;
            
            if (distance < minDistance) {
                minDistance = distance;
                bestMatch = username;
            }
        }

        if (bestMatch.isEmpty()) {
            response["success"] = false;
            response["message"] = "识别失败，未找到匹配的人脸";
            res.status = 401;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }

        // 生成 Token
        QString token = JwtHelper::generateToken(bestMatch);

        response["success"] = true;
        response["message"] = "登录成功";
        response["username"] = bestMatch;
        response["token"] = token;
        
        qInfo() << "✅ 用户" << bestMatch << "登录成功";
        
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
    });

    // 在独立线程中启动 HTTP 服务器
    QThread *serverThread = QThread::create([&]() {
        qInfo() << "🚀 HTTP 服务器启动在 http://0.0.0.0:3000";
        svr.listen("0.0.0.0", 3000);
    });
    serverThread->start();

    int ret = app.exec();

    // 清理
    svr.stop();
    serverThread->quit();
    serverThread->wait();
    delete serverThread;

    return ret;
}
