#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QByteArray>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include "DatabaseManager.h"
#include "FaceRecognizer.h"
#include "JwtHelper.h"
#include "PasswordUtils.h"
#include "LoginRateLimiter.h"
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

    // 初始化 libsodium(argon2id 密码哈希)
    if (!PasswordUtils::initialize())
    {
        qCritical() << "密码哈希库初始化失败,退出";
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

    // 限制工作线程数,防止高并发下线程/数据库连接爆炸。
    // 每个 socket 在其 keep-alive 生命周期内占用一个工作线程,故线程本地数据库连接数 ≈ 线程池大小,
    // 上限 32(+主线程)远低于 MySQL 默认 max_connections=151。
    const int workers = std::clamp(QThread::idealThreadCount() * 2, 4, 32);
    svr.new_task_queue = [workers] { return new httplib::ThreadPool(workers); };
    svr.set_read_timeout(30);   // 秒,防慢客户端长时间占用工作线程
    svr.set_write_timeout(30);

    // 限制单次请求体大小,防止超大 base64 图像耗尽内存(DoS)。
    // 人脸图像 base64 通常远小于此;/api/face/compare 一次携带两张图,故取 16MB 留足余量。
    constexpr size_t kMaxBodyBytes = 16ull * 1024 * 1024;
    svr.set_payload_max_length(kMaxBodyBytes);

    qInfo() << "✅ 工作线程池:" << workers << "线程(每线程独立数据库连接)";
    qInfo() << "✅ 请求体上限:" << (kMaxBodyBytes / (1024 * 1024)) << "MB";

    // 解析 CORS 白名单(从 ALLOWED_ORIGINS,逗号分隔)
    // 未设置或显式设为 "*" → 通配,适合本地开发;生产应配置具体源
    QSet<QString> allowedOrigins;
    bool corsWildcard = false;
    {
        QByteArray raw = qgetenv("ALLOWED_ORIGINS");
        if (raw.isEmpty()) {
            corsWildcard = true;
        } else {
            for (const QByteArray &piece : raw.split(',')) {
                QString trimmed = QString::fromUtf8(piece).trimmed();
                if (trimmed.isEmpty()) continue;
                if (trimmed == "*") { corsWildcard = true; allowedOrigins.clear(); break; }
                allowedOrigins.insert(trimmed);
            }
            if (allowedOrigins.isEmpty() && !corsWildcard) corsWildcard = true;
        }
    }
    if (corsWildcard) {
        qWarning() << "⚠️  CORS 使用通配 *(未设置 ALLOWED_ORIGINS),生产环境请配置具体源";
    } else {
        qInfo() << "✅ CORS 白名单:" << QStringList(allowedOrigins.values()).join(", ");
    }

    // 设置 CORS(允许前端跨域访问)
    svr.set_pre_routing_handler([corsWildcard, allowedOrigins](const httplib::Request &req, httplib::Response &res)
                                {
        QString origin = QString::fromStdString(req.get_header_value("Origin"));

        if (corsWildcard) {
            res.set_header("Access-Control-Allow-Origin", "*");
        } else if (!origin.isEmpty() && allowedOrigins.contains(origin)) {
            res.set_header("Access-Control-Allow-Origin", origin.toStdString());
            res.set_header("Vary", "Origin");
        }
        // 否则不发 Access-Control-Allow-Origin,浏览器会拦截

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

    LoginRateLimiter loginRateLimiter;  // 默认: 60 秒窗口 / 5 次失败上限
    FaceRoutes faceRoutes(db, recognizer, loginRateLimiter);
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
