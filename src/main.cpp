#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QByteArray>
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "JwtHelper.h"
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

    // 加载 JWT 密钥(从环境变量 JWT_SECRET)
    if (!JwtHelper::initialize())
    {
        qCritical() << "JWT 密钥初始化失败,退出";
        return -1;
    }

    // 读取数据库连接配置
    // 必填: DB_USER, DB_PASS
    // 可选: DB_HOST (默认 127.0.0.1)、DB_PORT (默认 3306)、DB_NAME (默认 face_recognition_db)
    QByteArray dbHost = qgetenv("DB_HOST");
    if (dbHost.isEmpty()) dbHost = "127.0.0.1";

    QByteArray dbName = qgetenv("DB_NAME");
    if (dbName.isEmpty()) dbName = "face_recognition_db";

    int dbPort = 3306;
    QByteArray dbPortRaw = qgetenv("DB_PORT");
    if (!dbPortRaw.isEmpty()) {
        bool ok = false;
        dbPort = dbPortRaw.toInt(&ok);
        if (!ok || dbPort <= 0 || dbPort > 65535) {
            qCritical() << "❌ DB_PORT 不是合法端口号:" << dbPortRaw;
            return -1;
        }
    }

    QByteArray dbUser = qgetenv("DB_USER");
    if (dbUser.isEmpty()) {
        qCritical() << "❌ 未设置环境变量 DB_USER";
        return -1;
    }

    QByteArray dbPass = qgetenv("DB_PASS");
    if (dbPass.isEmpty()) {
        qCritical() << "❌ 未设置环境变量 DB_PASS";
        return -1;
    }

    // 初始化数据库
    DatabaseManager db;
    if (!db.initialize(QString::fromUtf8(dbHost), dbPort,
                       QString::fromUtf8(dbName),
                       QString::fromUtf8(dbUser),
                       QString::fromUtf8(dbPass)))
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
