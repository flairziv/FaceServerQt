#ifndef LOGINRATELIMITER_H
#define LOGINRATELIMITER_H

#include <QString>
#include <QHash>
#include <QQueue>
#include <QMutex>

// 简单的滑动窗口失败限速器,按调用方提供的 key 跟踪失败次数。
// 默认: 60 秒窗口内累计 5 次失败 → 拒绝直到窗口滑出最早一次失败。
//
// key 由调用方组合: 登录用 "login|<IP>|<用户名>"、改密用 "pwd|<IP>|<用户名>",
// 即按 IP+用户名 联合限流——既能挡住单 IP 的暴力尝试,又不会让攻击者仅凭用户名
// 把受害者全局锁死(不同 IP 互不影响)。
// 残留: 分布式(多 IP)针对同一账号的低速撞库不受单维度限制,可后续叠加账号级/全局阈值。
//
// 线程安全:所有公共方法持锁,可在并发请求处理器中直接共享一个实例。
class LoginRateLimiter
{
public:
    LoginRateLimiter(int windowSeconds = 60, int maxFailures = 5);

    // 检查 key 是否被限速。返回 0 表示放行,>0 表示需等待的秒数。
    int secondsUntilAllowed(const QString &key);

    // 记录一次失败(由路由在每个失败分支调用)。
    void recordFailure(const QString &key);

    // 记录一次成功,清空该 key 的失败历史。
    void recordSuccess(const QString &key);

private:
    int m_windowSeconds;
    int m_maxFailures;
    QMutex m_mutex;
    QHash<QString, QQueue<qint64>> m_failures;  // key → 失败时间戳(秒)
};

#endif // LOGINRATELIMITER_H
