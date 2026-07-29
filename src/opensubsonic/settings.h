#ifndef OPENSUBSONIC_SETTINGS_H
#define OPENSUBSONIC_SETTINGS_H

#include <QString>
#include <QObject>

#define CFG_SECTION "opensubsonic"
#define CFG_SERVER_URL "server_url"
#define CFG_USERNAME "username"
#define CFG_PASSWORD_TOKEN "password_token"
#define CFG_PASSWORD_SALT  "password_salt"
#define CFG_ENABLED "enabled"
#define CFG_SCROBBLE_ENABLED "scrobble_enabled"
#define CFG_SCROBBLE_THRESHOLD "scrobble_threshold"

class OpenSubsonicSettings
{
public:
    static QString getServerUrl();
    static void setServerUrl(const QString& url);

    static QString getUsername();
    static void setUsername(const QString& username);

    static QString getPasswordToken();
    static QString getPasswordSalt();
    // Hashes plaintext with a fresh random salt and stores token+salt
    static void setPassword(const QString& password);
    // Computes token+salt from plaintext without storing (e.g. for UI test)
    static void computeTokenAndSalt(const QString& password, QString& token, QString& salt);

    static bool isEnabled();
    static void setEnabled(bool enabled);

    static bool isScrobbleEnabled();
    static void setScrobbleEnabled(bool enabled);

    // Percentage of track that must play before scrobbling (1–100)
    static int getScrobbleThreshold();
    static void setScrobbleThreshold(int percent);

    static bool testConnection();
};

#endif // OPENSUBSONIC_SETTINGS_H
