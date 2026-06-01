#include "FaceRoutes.h"
#include "PasswordUtils.h"
#include "JwtHelper.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

FaceRoutes::FaceRoutes(DatabaseManager &db, FaceRecognizer &recognizer, LoginRateLimiter &rateLimiter)
    : m_db(db), m_recognizer(recognizer), m_rateLimiter(rateLimiter)
{
}

void FaceRoutes::registerRoutes(httplib::Server &svr)
{
    svr.Post("/api/face/register", [this](const auto &req, auto &res) {
        handleRegister(req, res);
    });

    svr.Post("/api/face/login", [this](const auto &req, auto &res) {
        handleLogin(req, res);
    });

    svr.Post("/api/face/detect", [this](const auto &req, auto &res) {
        handleDetect(req, res);
    });

    svr.Post("/api/face/compare", [this](const auto &req, auto &res) {
        handleCompare(req, res);
    });

    svr.Post("/api/face/search", [this](const auto &req, auto &res) {
        handleSearch(req, res);
    });
}

void FaceRoutes::handleRegister(const httplib::Request &req, httplib::Response &res)
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
    if (m_db.userExists(username)) {
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
        descriptor = m_recognizer.extractDescriptorFromBase64(image);
        if (descriptor.isEmpty()) {
            response["success"] = false;
            response["message"] = "未检测到人脸,请确保光线充足并正对摄像头";
            res.status = 400;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }
    }

    // 密码哈希处理(argon2id)
    QString hashedPassword;
    if (!password.isEmpty()) {
        hashedPassword = PasswordUtils::hashPassword(password);
        if (hashedPassword.isEmpty()) {
            response["success"] = false;
            response["message"] = "注册失败,请稍后重试";
            res.status = 500;
            res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                           "application/json");
            return;
        }
    }

    // 保存到数据库
    if (!m_db.insertUser(username, descriptor, hashedPassword)) {
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
                   "application/json");
}

void FaceRoutes::handleLogin(const httplib::Request &req, httplib::Response &res)
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

    // 登录限流键: 按 IP+用户名 联合维度,避免攻击者仅凭用户名把受害者全局锁死
    const QString rlKey = QStringLiteral("login|")
        + QString::fromStdString(req.remote_addr) + "|" + username;

    // 登录限流检查(滑动窗口)
    int waitSecs = m_rateLimiter.secondsUntilAllowed(rlKey);
    if (waitSecs > 0) {
        qWarning() << "⏱ 用户" << username << "触发登录限流,需等待" << waitSecs << "秒";
        response["success"] = false;
        response["message"] = QString("登录失败次数过多,请 %1 秒后再试").arg(waitSecs);
        res.status = 429;
        res.set_header("Retry-After", std::to_string(waitSecs));
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 统一的认证失败响应:无论账号不存在 / 密码错 / 人脸不符 / 未录入,
    // 一律返回相同状态码与文案,避免账号枚举与"哪一环出错"的状态探测。
    // 服务端日志仍记录具体原因便于排查,但不下发给客户端。
    // 已知残留:密码错会在人脸推理前快速返回,存在计时侧信道,后续可用恒定耗时缓解。
    auto rejectAuth = [&]() {
        m_rateLimiter.recordFailure(rlKey);
        QJsonObject r;
        r["success"] = false;
        r["message"] = "认证失败:账号、密码或人脸信息不正确";
        res.status = 401;
        res.set_content(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
    };

    // 第一步: 验证用户是否存在
    if (!m_db.userExists(username)) {
        qWarning() << "✗ 登录失败:用户" << username << "不存在";
        rejectAuth();
        return;
    }

    // 第二步: 验证密码
    QString storedPassword = m_db.getUserPassword(username);
    if (storedPassword.isEmpty()) {
        qWarning() << "✗ 登录失败:用户" << username << "未设置密码";
        rejectAuth();
        return;
    }

    if (!PasswordUtils::verifyPassword(password, storedPassword)) {
        qWarning() << "✗ 用户" << username << "密码验证失败";
        rejectAuth();
        return;
    }

    qInfo() << "✓ 用户" << username << "密码验证通过";

    // 第三步: 验证人脸。未检测到人脸也按统一失败处理,
    // 否则"密码已正确但图像无脸→提示未检测到人脸"会变成密码正确性预言机。
    QVector<float> descriptor = m_recognizer.extractDescriptorFromBase64(image);
    if (descriptor.isEmpty()) {
        qWarning() << "✗ 登录失败:用户" << username << "提交图像未检测到人脸";
        rejectAuth();
        return;
    }

    QVector<float> storedDescriptor = m_db.getUserDescriptor(username);
    if (storedDescriptor.isEmpty()) {
        qWarning() << "✗ 登录失败:用户" << username << "未录入人脸";
        rejectAuth();
        return;
    }

    double distance = FaceRecognizer::computeDistance(descriptor, storedDescriptor);
    qInfo() << "与用户" << username << "的人脸距离:" << distance;

    if (distance >= FaceRecognizer::DEFAULT_THRESHOLD) {
        qWarning() << "✗ 用户" << username << "人脸验证失败 (距离:" << distance << ")";
        rejectAuth();
        return;
    }

    qInfo() << "✓ 用户" << username << "人脸验证通过 (距离:" << distance << ")";

    // 认证通过,清空失败计数
    m_rateLimiter.recordSuccess(rlKey);

    // 旧版无盐 SHA-256 哈希在登录成功后透明升级为 argon2id
    if (PasswordUtils::needsRehash(storedPassword)) {
        QString upgraded = PasswordUtils::hashPassword(password);
        if (!upgraded.isEmpty()) {
            m_db.updateUserPassword(username, upgraded);
            qInfo() << "🔐 用户" << username << "密码哈希已升级为 argon2id";
        }
    }

    QString token = JwtHelper::generateToken(username);
    m_db.updateLastLogin(username);

    response["success"] = true;
    response["message"] = "认证成功";
    response["username"] = username;
    response["token"] = token;
    response["authMethod"] = "密码+人脸双重认证";
    response["faceDistance"] = distance;

    qInfo() << "🎉 用户" << username << "登录成功";

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void FaceRoutes::handleDetect(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到人脸检测请求";

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString image = bodyJson["image"].toString();

    QJsonObject response;

    if (image.isEmpty()) {
        response["success"] = false;
        response["message"] = "图像不能为空";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    QVector<float> descriptor = m_recognizer.extractDescriptorFromBase64(image);
    bool faceDetected = !descriptor.isEmpty();

    response["success"] = true;
    response["faceDetected"] = faceDetected;
    response["message"] = faceDetected ? "检测到人脸" : "未检测到人脸";

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void FaceRoutes::handleCompare(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到人脸比对请求";

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString image1 = bodyJson["image1"].toString();
    QString image2 = bodyJson["image2"].toString();

    QJsonObject response;

    if (image1.isEmpty() || image2.isEmpty()) {
        response["success"] = false;
        response["message"] = "需要提供两张图像";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    QVector<float> descriptor1 = m_recognizer.extractDescriptorFromBase64(image1);
    if (descriptor1.isEmpty()) {
        response["success"] = false;
        response["message"] = "第一张图像未检测到人脸";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    QVector<float> descriptor2 = m_recognizer.extractDescriptorFromBase64(image2);
    if (descriptor2.isEmpty()) {
        response["success"] = false;
        response["message"] = "第二张图像未检测到人脸";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    double distance = FaceRecognizer::computeDistance(descriptor1, descriptor2);
    double similarity = qMax(0.0, (1.0 - distance)) * 100; // 转换为相似度百分比
    bool isSamePerson = distance < FaceRecognizer::DEFAULT_THRESHOLD;

    response["success"] = true;
    response["distance"] = distance;
    response["similarity"] = QString::number(similarity, 'f', 2) + "%";
    response["isSamePerson"] = isSamePerson;
    response["threshold"] = FaceRecognizer::DEFAULT_THRESHOLD;

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}

void FaceRoutes::handleSearch(const httplib::Request &req, httplib::Response &res)
{
    qInfo() << "收到人脸搜索请求";

    auto bodyJson = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
    QString image = bodyJson["image"].toString();

    QJsonObject response;

    if (image.isEmpty()) {
        response["success"] = false;
        response["message"] = "图像不能为空";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    QVector<float> descriptor = m_recognizer.extractDescriptorFromBase64(image);
    if (descriptor.isEmpty()) {
        response["success"] = false;
        response["message"] = "未检测到人脸";
        res.status = 400;
        res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                       "application/json");
        return;
    }

    // 一次查询拿到所有已录入人脸的用户 + 特征向量,避免 N+1
    QVector<QPair<QString, QVector<float>>> users = m_db.getAllUserDescriptors();
    QString bestMatch;
    double bestDistance = 999.0;

    for (const auto &user : users) {
        double distance = FaceRecognizer::computeDistance(descriptor, user.second);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatch = user.first;
        }
    }

    if (bestMatch.isEmpty()) {
        response["success"] = false;
        response["message"] = "数据库中没有人脸数据";
        res.status = 404;
    } else {
        response["success"] = true;
        response["bestMatch"] = bestMatch;
        response["distance"] = bestDistance;
        response["isMatch"] = bestDistance < FaceRecognizer::DEFAULT_THRESHOLD;
        response["threshold"] = FaceRecognizer::DEFAULT_THRESHOLD;

        qInfo() << "🔍 人脸搜索结果: 最匹配用户" << bestMatch << "(距离:" << bestDistance << ")";
    }

    res.set_content(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString(),
                   "application/json");
}
