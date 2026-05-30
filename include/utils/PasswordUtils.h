#ifndef PASSWORDUTILS_H
#define PASSWORDUTILS_H

#include <QString>

// 密码哈希工具。
// 当前算法: argon2id(经 libsodium),自带随机盐与工作因子,哈希串自描述。
// 兼容旧版: 历史账号的 password_hash 为无盐 SHA-256(64位十六进制),
//           verifyPassword 仍可校验,并在登录成功后透明升级为 argon2id。
class PasswordUtils
{
public:
    // 初始化 libsodium(进程启动时调用一次)。失败返回 false。
    static bool initialize();

    // 用 argon2id 对密码加盐哈希,返回自描述哈希串("$argon2id$...")。
    // 失败(如内存不足)返回空字符串。
    static QString hashPassword(const QString &password);

    // 校验密码。storedHash 以 '$' 开头按 argon2id 校验;
    // 否则视为旧版无盐 SHA-256 十六进制。
    static bool verifyPassword(const QString &password, const QString &storedHash);

    // 是否需要重新哈希(旧版 SHA-256,或 argon2 参数偏弱)。
    // 用于登录成功后将旧哈希透明升级为 argon2id。
    static bool needsRehash(const QString &storedHash);

private:
    // 旧版算法: 无盐 SHA-256 十六进制。仅用于校验历史数据。
    static QString legacySha256Hex(const QString &password);
};

#endif // PASSWORDUTILS_H
