#include "JwtHelper.h"
#include <jwt-cpp/jwt.h>
#include <chrono>
#include <QByteArray>
#include <QDebug>

// HS256 推荐密钥长度(字节)
static constexpr int MIN_SECRET_LENGTH = 32;

// 运行时从环境变量加载,initialize() 调用前为空
QString JwtHelper::s_secretKey;

bool JwtHelper::initialize()
{
    QByteArray envSecret = qgetenv("JWT_SECRET");
    if (envSecret.isEmpty()) {
        qCritical() << "❌ 未设置环境变量 JWT_SECRET";
        qCritical() << "   请先 export JWT_SECRET=\"<至少 32 字节的随机字符串>\" 再启动";
        return false;
    }
    if (envSecret.size() < MIN_SECRET_LENGTH) {
        qCritical() << "❌ JWT_SECRET 长度不足" << MIN_SECRET_LENGTH
                    << "字节,当前:" << envSecret.size();
        return false;
    }
    s_secretKey = QString::fromUtf8(envSecret);
    qInfo() << "✅ JWT 密钥已加载(长度" << envSecret.size() << "字节)";
    return true;
}

QString JwtHelper::generateToken(const QString &username, int expiresInHours)
{
    try {
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        // 计算过期时间(默认24小时后)
        auto exp = now + std::chrono::hours(expiresInHours);

        std::string token = jwt::create()
            .set_issuer("FaceRecognitionServer")    // 设置发行人（issuer）
            .set_type("JWT")    // 设置 token 类型
            .set_issued_at(now) // 设置签发时间
            .set_expires_at(exp)    // 设置过期时间
            .set_payload_claim("username", jwt::claim(username.toStdString()))  // 设置 payload 中的用户名
            .sign(jwt::algorithm::hs256{s_secretKey.toStdString()}); // 使用 HS256 算法和密钥进行签名

        return QString::fromStdString(token);
    } catch (const std::exception &e) {
        qCritical() << "生成 Token 失败:" << e.what();
        return QString();
    }
}

bool JwtHelper::verifyToken(const QString &token, QString &username)
{
    try {
        // 解码 JWT
        auto decoded = jwt::decode(token.toStdString());

        // 创建 JWT 验证器
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{s_secretKey.toStdString()})
            .with_issuer("FaceRecognitionServer");

        // 验证 JWT 是否有效
        verifier.verify(decoded);

        // 提取 payload 中的 "username" 声明
        username = QString::fromStdString(
            decoded.get_payload_claim("username").as_string()
        );

        return true;
    } catch (const std::exception &e) {
        qWarning() << "Token 验证失败:" << e.what();
        return false;
    }
}
