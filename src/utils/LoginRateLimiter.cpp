#include "LoginRateLimiter.h"
#include <QMutexLocker>
#include <QDateTime>

LoginRateLimiter::LoginRateLimiter(int windowSeconds, int maxFailures)
    : m_windowSeconds(windowSeconds), m_maxFailures(maxFailures)
{
}

static void evictExpired(QQueue<qint64> &q, qint64 now, int window)
{
    while (!q.isEmpty() && q.head() <= now - window) {
        q.dequeue();
    }
}

int LoginRateLimiter::secondsUntilAllowed(const QString &key)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_failures.find(key);
    if (it == m_failures.end()) return 0;

    qint64 now = QDateTime::currentSecsSinceEpoch();
    evictExpired(it.value(), now, m_windowSeconds);

    if (it.value().isEmpty()) {
        m_failures.erase(it);
        return 0;
    }
    if (it.value().size() < m_maxFailures) return 0;

    // 已到上限,等到最早一次失败滑出窗口为止
    qint64 retryAt = it.value().head() + m_windowSeconds;
    return retryAt > now ? static_cast<int>(retryAt - now) : 0;
}

void LoginRateLimiter::recordFailure(const QString &key)
{
    QMutexLocker locker(&m_mutex);
    qint64 now = QDateTime::currentSecsSinceEpoch();
    auto &q = m_failures[key];
    evictExpired(q, now, m_windowSeconds);
    q.enqueue(now);
}

void LoginRateLimiter::recordSuccess(const QString &key)
{
    QMutexLocker locker(&m_mutex);
    m_failures.remove(key);
}
