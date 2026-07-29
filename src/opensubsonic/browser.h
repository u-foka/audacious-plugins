#ifndef OPENSUBSONIC_BROWSER_H
#define OPENSUBSONIC_BROWSER_H

#include <QWidget>
#include <QListWidget>
#include <QListView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QJsonArray>
#include <QStyledItemDelegate>
#include <memory>

class OpenSubsonicClient;

class AlbumDetailDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit AlbumDetailDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class OpenSubsonicBrowser : public QWidget
{
    Q_OBJECT

public:
    OpenSubsonicBrowser(QWidget* parent = nullptr);
    ~OpenSubsonicBrowser();

private slots:
    void onRefresh();
    void onModeChanged(int index);
    void onArtistSearchChanged(const QString& text);
    void onAlbumSearchChanged(const QString& text);
    void onTrackSearchChanged(const QString& text);
    void onArtistSelected(int row);
    void onAlbumSelected(int row);
    void onAlbumDoubleClicked(QListWidgetItem* item);
    void onTrackDoubleClicked(QListWidgetItem* item);
    void onOpenAlbum();
    void onEnqueueAlbum();
    void onOpenTracks();
    void onEnqueueTracks();
    void onArtistsLoaded(const QJsonArray& artists);
    void onArtistDetailsLoaded(const QJsonArray& albums);
    void onAllAlbumsLoaded(const QJsonArray& albums);
    void onAlbumLoaded(const QJsonArray& songs);
    void onConnectionStatusChanged(bool connected);
    void onCoverArtReady(const QString& albumId, const QByteArray& imageData);

private:
    void setupUI();
    void loadArtists();
    void updateStatus(const QString& message);
    void addTracksToPlaylist(const QJsonArray& songs, bool enqueue);
    QJsonArray convertSelectedToArray(const QList<QListWidgetItem*>& items);
    void filterArtistList(const QString& filter);
    void filterAlbumList(const QString& filter);
    void filterTrackList(const QString& filter);
    void switchAlbumViewMode(bool detailMode);
    void populateAlbumList(const QJsonArray& albums);
    QListWidgetItem* getCurrentAlbumItem();
    int getCurrentAlbumRow();

    std::unique_ptr<OpenSubsonicClient> m_client;
    
    QLabel* m_statusLabel;
    QPushButton* m_refreshButton;
    QComboBox* m_modeCombo;
    QWidget* m_artistPane;  // hidden in non-browse modes
    
    // Search boxes
    QLineEdit* m_artistSearch;
    QLineEdit* m_albumSearch;
    QLineEdit* m_trackSearch;
    
    // Lists
    QListWidget* m_artistList;
    QListWidget* m_albumListDetail;  // Detail view
    QListView* m_albumListIcon;      // Icon/tile view
    QWidget* m_albumViewContainer;   // Container for both views
    QListWidget* m_trackList;
    
    // Album view mode toggle
    QPushButton* m_albumDetailButton;
    QPushButton* m_albumIconButton;
    bool m_albumDetailMode;  // true for detail, false for icon
    
    // Buttons for albums and tracks
    QPushButton* m_openAlbumButton;
    QPushButton* m_enqueueAlbumButton;
    QPushButton* m_openTracksButton;
    QPushButton* m_enqueueTracksButton;
    
    // Full data for filtering
    QJsonArray m_allArtists;
    QJsonArray m_allAlbums;
    QJsonArray m_allTracks;
};

#endif // OPENSUBSONIC_BROWSER_H
