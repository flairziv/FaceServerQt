#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "httplib.h"
#include "UserRoutes.h"
#include "FaceRoutes.h"
#include "AuthRoutes.h"
#include "SystemRoutes.h"

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
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");

        if (req.method == "OPTIONS") {
            res.status = 200;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled; });

    // 注册所有路由模块
    qInfo() << "正在注册API路由...";

    SystemRoutes systemRoutes(db);
    systemRoutes.registerRoutes(svr);

    FaceRoutes faceRoutes(db, recognizer);
    faceRoutes.registerRoutes(svr);

    UserRoutes userRoutes(db, recognizer);
    userRoutes.registerRoutes(svr);

    AuthRoutes authRoutes;
    authRoutes.registerRoutes(svr);

    qInfo() << "✅ 所有API路由注册完成";

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
