#include "PasswordUtils.h"
#include <QCryptographicHash>
#include <QByteArray>
#include <QDebug>
#include <sodium.h>

bool PasswordUtils::initialize()
{
    // sodium_init(): 0 成功,1 已初始化,<0 失败。
    if (sodium_init() < 0) {
        qCritical() << "❌ libsodium 初始化失败";
        return false;
    }
    qInfo() << "✅ libsodium 初始化成功(argon2id 密码哈希)";
    return true;
}

QString PasswordUtils::hashPassword(const QString &password)
{
    const QByteArray pw = password.toUtf8();
    char encoded[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str(encoded,
                          pw.constData(),
                          static_cast<unsigned long long>(pw.size()),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        // 哈希失败通常意味着内存不足
        qWarning() << "⚠ argon2id 哈希失败(内存不足?)";
        return QString();
    }
    return QString::fromUtf8(encoded);
}

bool PasswordUtils::verifyPassword(const QString &password, const QString &storedHash)
{
    if (storedHash.isEmpty()) {
        return false;
    }

    // 旧版: 无盐 SHA-256 十六进制(不含 '$')
    if (!storedHash.startsWith('$')) {
        return storedHash == legacySha256Hex(password);
    }

    // argon2id
    const QByteArray pw = password.toUtf8();
    const QByteArray hash = storedHash.toUtf8();
    return crypto_pwhash_str_verify(hash.constData(),
                                    pw.constData(),
                                    static_cast<unsigned long long>(pw.size())) == 0;
}

bool PasswordUtils::needsRehash(const QString &storedHash)
{
    if (storedHash.isEmpty()) {
        return false;
    }
    // 旧版无盐 SHA-256 → 需升级
    if (!storedHash.startsWith('$')) {
        return true;
    }
    // argon2 参数偏弱(或哈希串无法识别)→ 需升级。
    // 返回 0 表示无需重哈希;1 表示参数不同;-1 表示无法识别(也按需升级处理)。
    const QByteArray hash = storedHash.toUtf8();
    return crypto_pwhash_str_needs_rehash(hash.constData(),
                                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0;
}

QString PasswordUtils::legacySha256Hex(const QString &password)
{
    return QString(QCryptographicHash::hash(password.toUtf8(),
                                            QCryptographicHash::Sha256)
                       .toHex());
}
