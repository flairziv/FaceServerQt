#include "PasswordUtils.h"
#include <QCryptographicHash>

QString PasswordUtils::hashPassword(const QString &password)
{
    return QString(QCryptographicHash::hash(password.toUtf8(),
                                            QCryptographicHash::Sha256)
                       .toHex());
}
