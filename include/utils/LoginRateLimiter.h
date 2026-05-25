#ifndef LOGINRATELIMITER_H
#define LOGINRATELIMITER_H

#include <QString>
#include <QHash>
#include <QQueue>
#include <QMutex>

// 简单的滑动窗口登录限速器,按用户名跟踪失败次数
// 默认: 60 秒窗口内累计 5 次失败 → 拒绝直到窗口滑出最早一次失败
//
// 已知权衡:仅按用户名限流,攻击者可通过提交错误凭据短时间内 DOS 已知用户;
// 进一步加固应引入 IP + 用户名联合维度,见后续迭代。
//
// 线程安全:所有公共方法持锁,可在并发请求处理器中直接共享一个实例。
class LoginRateLimiter
{
public:
    LoginRateLimiter(int windowSeconds = 60, int maxFailures = 5);

    // 检查 username 是否被限速。返回 0 表示放行,>0 表示需等待的秒数。
    int secondsUntilAllowed(const QString &username);

    // 记录一次失败(由路由在每个 401 分支调用)。
    void recordFailure(const QString &username);

    // 记录一次成功,清空该用户的失败历史。
    void recordSuccess(const QString &username);

private:
    int m_windowSeconds;
    int m_maxFailures;
    QMutex m_mutex;
    QHash<QString, QQueue<qint64>> m_failures;  // username → 失败时间戳(秒)
};

#endif // LOGINRATELIMITER_H
