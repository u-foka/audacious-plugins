#include "settings.h"
#include <libaudcore/runtime.h>
#include <libaudcore/audstrings.h>
#include <QCryptographicHash>
#include <QRandomGenerator>

QString OpenSubsonicSettings::getServerUrl()
{
    return QString(aud_get_str(CFG_SECTION, CFG_SERVER_URL));
}

void OpenSubsonicSettings::setServerUrl(const QString& url)
{
    aud_set_str(CFG_SECTION, CFG_SERVER_URL, url.toUtf8().constData());
}

QString OpenSubsonicSettings::getUsername()
{
    return QString(aud_get_str(CFG_SECTION, CFG_USERNAME));
}

void OpenSubsonicSettings::setUsername(const QString& username)
{
    aud_set_str(CFG_SECTION, CFG_USERNAME, username.toUtf8().constData());
}

QString OpenSubsonicSettings::getPasswordToken()
{
    return QString(aud_get_str(CFG_SECTION, CFG_PASSWORD_TOKEN));
}

QString OpenSubsonicSettings::getPasswordSalt()
{
    return QString(aud_get_str(CFG_SECTION, CFG_PASSWORD_SALT));
}

void OpenSubsonicSettings::computeTokenAndSalt(const QString& password, QString& token, QString& salt)
{
    QByteArray saltRaw(16, 0);
    quint32 * p = reinterpret_cast<quint32 *>(saltRaw.data());
    for (int i = 0; i < 4; ++i)
        p[i] = QRandomGenerator::global()->generate();
    salt = QString::fromLatin1(saltRaw.toHex());
    token = QString::fromLatin1(
        QCryptographicHash::hash((password + salt).toUtf8(),
                                 QCryptographicHash::Md5).toHex());
}

void OpenSubsonicSettings::setPassword(const QString& password)
{
    QString token, salt;
    computeTokenAndSalt(password, token, salt);
    aud_set_str(CFG_SECTION, CFG_PASSWORD_TOKEN, token.toUtf8().constData());
    aud_set_str(CFG_SECTION, CFG_PASSWORD_SALT,  salt.toUtf8().constData());
}

bool OpenSubsonicSettings::isEnabled()
{
    return aud_get_bool(CFG_SECTION, CFG_ENABLED);
}

void OpenSubsonicSettings::setEnabled(bool enabled)
{
    aud_set_bool(CFG_SECTION, CFG_ENABLED, enabled);
}

bool OpenSubsonicSettings::testConnection()
{
    // TODO: Implement connection test
    return true;
}

bool OpenSubsonicSettings::isScrobbleEnabled()
{
    return aud_get_bool(CFG_SECTION, CFG_SCROBBLE_ENABLED);
}

void OpenSubsonicSettings::setScrobbleEnabled(bool enabled)
{
    aud_set_bool(CFG_SECTION, CFG_SCROBBLE_ENABLED, enabled);
}

int OpenSubsonicSettings::getScrobbleThreshold()
{
    int v = aud_get_int(CFG_SECTION, CFG_SCROBBLE_THRESHOLD);
    return (v <= 0 || v > 100) ? 50 : v;
}

void OpenSubsonicSettings::setScrobbleThreshold(int percent)
{
    aud_set_int(CFG_SECTION, CFG_SCROBBLE_THRESHOLD, percent);
}
