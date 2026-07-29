#ifndef OPENSUBSONIC_API_H
#define OPENSUBSONIC_API_H

#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMap>
#include <memory>

class OpenSubsonicClient : public QObject
{
    Q_OBJECT

public:
    OpenSubsonicClient(const QString& serverUrl, const QString& username,
                       const QString& token, const QString& salt);
    ~OpenSubsonicClient();

    // Synchronous connection test
    bool ping();
    
    // Asynchronous API calls
    void getArtists();
    void getArtist(const QString& artistId);
    void getAllAlbums();
    void getAlbumList(const QString& type, int size = 500);
    void getAlbum(const QString& albumId);
    void search(const QString& query);
    
    // Streaming
    QString getStreamUrl(const QString& trackId);
    
    // Cover art
    QByteArray getCoverArt(const QString& coverArtId);
    void getCoverArtAsync(const QString& coverArtId, const QString& albumId);

    // Scrobbling
    void scrobble(const QString& trackId);

    // OpenSubsonic reportPlayback extension
    enum class PlaybackState { Starting, Playing, Paused, Stopped };
    void reportPlayback(const QString& trackId, int positionMs, PlaybackState state);
    void reportPlaybackSync(const QString& trackId, int positionMs, PlaybackState state);

signals:
    void artistsReady(const QJsonArray& artists);
    void artistDetailsReady(const QJsonArray& albums);
    void allAlbumsReady(const QJsonArray& albums);
    void albumReady(const QJsonArray& songs);
    void coverArtReady(const QString& albumId, const QByteArray& imageData);
    void error(const QString& message);
    void connectionStatusChanged(bool connected);

private slots:
    void onArtistsReplyFinished();
    void onArtistReplyFinished();
    void onAllAlbumsReplyFinished();
    void onAlbumReplyFinished();
    void onCoverArtReplyFinished();

private:
    QString buildUrl(const QString& method);
    QJsonObject parseResponse(const QByteArray& data);

    QString m_serverUrl;
    QString m_username;
    QString m_token;
    QString m_salt;
    QString m_clientName = "Audacious-OpenSubsonic";
    QString m_clientVersion = "1.0";
    
    std::unique_ptr<QNetworkAccessManager> m_networkManager;
    QNetworkReply* m_currentReply = nullptr;
    QMap<QNetworkReply*, QString> m_coverArtReplies;  // Track cover art requests
};

#endif // OPENSUBSONIC_API_H
