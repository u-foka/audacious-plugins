#include "api.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QTimer>
#include <QDateTime>

OpenSubsonicClient::OpenSubsonicClient(const QString& serverUrl, const QString& username,
                                       const QString& token, const QString& salt)
    : m_serverUrl(serverUrl), m_username(username), m_token(token), m_salt(salt)
{
    m_networkManager = std::make_unique<QNetworkAccessManager>(this);
}

OpenSubsonicClient::~OpenSubsonicClient()
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
}

QString OpenSubsonicClient::buildUrl(const QString& method)
{
    QUrl url(m_serverUrl);
    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    url.setPath(path + "rest/" + method + ".view");

    QUrlQuery query;
    query.addQueryItem("u", m_username);
    query.addQueryItem("c", m_clientName);
    query.addQueryItem("v", m_clientVersion);
    query.addQueryItem("f", "json");
    query.addQueryItem("t", m_token);
    query.addQueryItem("s", m_salt);

    url.setQuery(query);
    return url.toString();
}

QJsonObject OpenSubsonicClient::parseResponse(const QByteArray& data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return QJsonObject();

    QJsonObject root = doc.object();
    QJsonObject response = root.value("subsonic-response").toObject();
    
    QString status = response.value("status").toString();
    if (status != "ok")
    {
        QString error = response.value("error").toObject().value("message").toString();
        emit this->error(!error.isEmpty() ? error : "Server returned error");
        return QJsonObject();
    }

    return response;
}

bool OpenSubsonicClient::ping()
{
    QString urlString = buildUrl("ping");
    QUrl url(urlString);
    QNetworkRequest request(url);

    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(request);
    
    bool success = false;
    
    connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            QJsonObject response = parseResponse(reply->readAll());
            success = !response.isEmpty();
        }
        loop.quit();
    });

    loop.exec();
    reply->deleteLater();
    
    if (success)
        emit connectionStatusChanged(true);
    
    return success;
}

void OpenSubsonicClient::getArtists()
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    QString urlString = buildUrl("getArtists");
    QUrl url(urlString);
    QNetworkRequest request(url);

    m_currentReply = m_networkManager->get(request);

    connect(m_currentReply, &QNetworkReply::finished, this, &OpenSubsonicClient::onArtistsReplyFinished);
}

void OpenSubsonicClient::onArtistsReplyFinished()
{
    if (!m_currentReply)
        return;

    if (m_currentReply->error() != QNetworkReply::NoError)
    {
        emit error(QString("Network error: %1").arg(m_currentReply->errorString()));
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QJsonObject response = parseResponse(m_currentReply->readAll());
    if (!response.isEmpty())
    {
        // getArtists response: { "artists": { "index": [ { "artist": [...] } ] } }
        QJsonObject artistsObj = response.value("artists").toObject();
        QJsonArray indexArray = artistsObj.value("index").toArray();

        QJsonArray allArtists;
        for (const QJsonValue& indexVal : indexArray)
        {
            QJsonArray artists = indexVal.toObject().value("artist").toArray();
            for (const QJsonValue& artist : artists)
                allArtists.append(artist);
        }

        emit artistsReady(allArtists);
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void OpenSubsonicClient::getArtist(const QString& artistId)
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    QUrl url(buildUrl("getArtist"));
    QUrlQuery query(url.query());
    query.addQueryItem("id", artistId);
    url.setQuery(query);

    m_currentReply = m_networkManager->get(QNetworkRequest(url));
    connect(m_currentReply, &QNetworkReply::finished, this, &OpenSubsonicClient::onArtistReplyFinished);
}

void OpenSubsonicClient::getAlbum(const QString& albumId)
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    QUrl url(buildUrl("getAlbum"));
    QUrlQuery query(url.query());
    query.addQueryItem("id", albumId);
    url.setQuery(query);

    m_currentReply = m_networkManager->get(QNetworkRequest(url));
    connect(m_currentReply, &QNetworkReply::finished, this, &OpenSubsonicClient::onAlbumReplyFinished);
}

void OpenSubsonicClient::onArtistReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    if (reply->error() != QNetworkReply::NoError)
    {
        emit error(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        if (m_currentReply == reply)
            m_currentReply = nullptr;
        return;
    }

    QJsonObject response = parseResponse(reply->readAll());
    if (!response.isEmpty())
    {
        QJsonObject artist = response.value("artist").toObject();
        QJsonArray albums = artist.value("album").toArray();
        emit artistDetailsReady(albums);
    }

    reply->deleteLater();
    if (m_currentReply == reply)
        m_currentReply = nullptr;
}

void OpenSubsonicClient::getAllAlbums()
{
    getAlbumList("alphabeticalByArtist");
}

void OpenSubsonicClient::getAlbumList(const QString& type, int size)
{
    if (m_currentReply)
    {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }

    QUrl url(buildUrl("getAlbumList2"));
    QUrlQuery query(url.query());
    query.addQueryItem("type", type);
    query.addQueryItem("size", QString::number(size));
    url.setQuery(query);

    QNetworkRequest request(url);
    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &OpenSubsonicClient::onAllAlbumsReplyFinished);
}

void OpenSubsonicClient::onAllAlbumsReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    if (reply->error() != QNetworkReply::NoError)
    {
        emit error(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        if (m_currentReply == reply)
            m_currentReply = nullptr;
        return;
    }

    QJsonObject response = parseResponse(reply->readAll());
    if (!response.isEmpty())
    {
        QJsonArray albums = response.value("albumList2").toObject().value("album").toArray();
        emit allAlbumsReady(albums);
    }

    reply->deleteLater();
    if (m_currentReply == reply)
        m_currentReply = nullptr;
}

void OpenSubsonicClient::onAlbumReplyFinished()
{
    if (!m_currentReply)
        return;

    if (m_currentReply->error() != QNetworkReply::NoError)
    {
        emit error(QString("Network error: %1").arg(m_currentReply->errorString()));
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QJsonObject response = parseResponse(m_currentReply->readAll());
    if (!response.isEmpty())
    {
        QJsonObject album = response.value("album").toObject();
        QJsonArray songs = album.value("song").toArray();
        emit albumReady(songs);
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void OpenSubsonicClient::search(const QString& query)
{
    // TODO: Implement search
}

QString OpenSubsonicClient::getStreamUrl(const QString& trackId)
{
    QUrl url(m_serverUrl);
    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    url.setPath(path + "rest/stream.view");

    QUrlQuery query;
    query.addQueryItem("u", m_username);
    query.addQueryItem("c", m_clientName);
    query.addQueryItem("v", m_clientVersion);
    query.addQueryItem("t", m_token);
    query.addQueryItem("s", m_salt);
    query.addQueryItem("id", trackId);
    url.setQuery(query);

    return url.toString();
}

QByteArray OpenSubsonicClient::getCoverArt(const QString& coverArtId)
{
    // Build URL for cover art endpoint
    QString baseUrl = m_serverUrl;
    if (!baseUrl.endsWith('/'))
        baseUrl += '/';
    
    QString urlString = baseUrl + "rest/getCoverArt.view";
    QUrl url(urlString);
    QUrlQuery query;
    query.addQueryItem("id", coverArtId);
    query.addQueryItem("u", m_username);
    query.addQueryItem("c", m_clientName);
    query.addQueryItem("v", m_clientVersion);
    // NOTE: Don't set format=json for getCoverArt - it returns binary image data, not JSON
    query.addQueryItem("t", m_token);
    query.addQueryItem("s", m_salt);

    url.setQuery(query);
    
    QNetworkRequest request(url);
    QEventLoop loop;
    QByteArray imageData;
    
    QNetworkReply* reply = m_networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            imageData = reply->readAll();
        }
        loop.quit();
    });

    loop.exec();
    reply->deleteLater();
    
    return imageData;
}

void OpenSubsonicClient::getCoverArtAsync(const QString& coverArtId, const QString& albumId)
{
    // Build URL for cover art endpoint
    QString baseUrl = m_serverUrl;
    if (!baseUrl.endsWith('/'))
        baseUrl += '/';
    
    QString urlString = baseUrl + "rest/getCoverArt.view";
    QUrl url(urlString);
    QUrlQuery query;
    query.addQueryItem("id", coverArtId);
    query.addQueryItem("u", m_username);
    query.addQueryItem("c", m_clientName);
    query.addQueryItem("v", m_clientVersion);
    query.addQueryItem("t", m_token);
    query.addQueryItem("s", m_salt);
    
    url.setQuery(query);
    
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    
    // Track this reply with its album ID
    m_coverArtReplies[reply] = albumId;
    
    // Connect to finished signal - will call onCoverArtReplyFinished
    connect(reply, &QNetworkReply::finished, this, &OpenSubsonicClient::onCoverArtReplyFinished);
}

void OpenSubsonicClient::onCoverArtReplyFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !m_coverArtReplies.contains(reply))
        return;
    
    QString albumId = m_coverArtReplies.take(reply);
    
    if (reply->error() == QNetworkReply::NoError)
    {
        QByteArray imageData = reply->readAll();
        emit coverArtReady(albumId, imageData);
    }
    
    reply->deleteLater();
}

void OpenSubsonicClient::scrobble(const QString& trackId)
{
    QString url = buildUrl("scrobble");
    QUrl qurl(url);
    QUrlQuery query(qurl);
    query.addQueryItem("id", trackId);
    query.addQueryItem("submission", "true");
    qurl.setQuery(query);

    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(qurl));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void OpenSubsonicClient::reportPlayback(const QString& trackId, int positionMs, PlaybackState state)
{
    static const char* stateStr[] = { "starting", "playing", "paused", "stopped" };

    QString url = buildUrl("reportPlayback");
    QUrl qurl(url);
    QUrlQuery query(qurl);
    query.addQueryItem("mediaId", trackId);
    query.addQueryItem("mediaType", "song");
    query.addQueryItem("positionMs", QString::number(positionMs));
    query.addQueryItem("state", stateStr[static_cast<int>(state)]);
    query.addQueryItem("playbackRate", "1.0");
    qurl.setQuery(query);

    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(qurl));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void OpenSubsonicClient::reportPlaybackSync(const QString& trackId, int positionMs, PlaybackState state)
{
    static const char* stateStr[] = { "starting", "playing", "paused", "stopped" };

    QString url = buildUrl("reportPlayback");
    QUrl qurl(url);
    QUrlQuery query(qurl);
    query.addQueryItem("mediaId", trackId);
    query.addQueryItem("mediaType", "song");
    query.addQueryItem("positionMs", QString::number(positionMs));
    query.addQueryItem("state", stateStr[static_cast<int>(state)]);
    query.addQueryItem("playbackRate", "1.0");
    qurl.setQuery(query);

    QEventLoop loop;
    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(qurl));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
}
