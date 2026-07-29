#include "browser.h"
#include "api.h"
#include "settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QStandardItemModel>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <libaudcore/drct.h>
#include <libaudcore/playlist.h>

// --- AlbumDetailDelegate Implementation ---
AlbumDetailDelegate::AlbumDetailDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void AlbumDetailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    
    // Background
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());
    else if (option.state & QStyle::State_MouseOver)
        painter->fillRect(option.rect, option.palette.midlight());
    
    // Get data
    QString title = index.data(Qt::DisplayRole).toString();
    QString tracks = index.data(Qt::UserRole + 1).toString();  // Track count
    QString duration = index.data(Qt::UserRole + 2).toString(); // Total duration
    QPixmap coverPixmap = index.data(Qt::UserRole + 3).value<QPixmap>();  // Album art (custom role)
    
    int left = option.rect.left() + 8;
    int top = option.rect.top() + 4;
    int coverSize = 40;  // 40x40 thumbnail
    
    // Draw album cover on the left
    if (!coverPixmap.isNull())
    {
        QPixmap scaledCover = coverPixmap.scaledToHeight(coverSize, Qt::SmoothTransformation);
        painter->drawPixmap(left, top, scaledCover);
        left += scaledCover.width() + 8;
    }
    
    painter->setPen(option.palette.text().color());
    
    // Title (bold)
    QFont boldFont = painter->font();
    boldFont.setBold(true);
    painter->setFont(boldFont);
    painter->drawText(left, top + 16, title);
    
    // Metadata (smaller text)
    QFont smallFont = painter->font();
    smallFont.setPointSize(smallFont.pointSize() - 2);
    painter->setFont(smallFont);
    painter->setPen(option.palette.mid().color());
    painter->drawText(left, top + 36, tracks);
    painter->drawText(left + 150, top + 36, duration);
    
    painter->restore();
}

QSize AlbumDetailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    return QSize(option.rect.width(), 56);
}

OpenSubsonicBrowser::OpenSubsonicBrowser(QWidget* parent)
    : QWidget(parent), m_albumDetailMode(true)
{
    setupUI();

    QString serverUrl = OpenSubsonicSettings::getServerUrl();
    QString username  = OpenSubsonicSettings::getUsername();
    QString token     = OpenSubsonicSettings::getPasswordToken();
    QString salt      = OpenSubsonicSettings::getPasswordSalt();

    if (serverUrl.isEmpty() || username.isEmpty())
    {
        updateStatus("Please configure server settings first");
        return;
    }

    m_client = std::make_unique<OpenSubsonicClient>(serverUrl, username, token, salt);

    // Connect signals
    connect(m_client.get(), &OpenSubsonicClient::error,
            this, [this](const QString& msg) { updateStatus(msg); });
    connect(m_client.get(), &OpenSubsonicClient::artistsReady,
            this, &OpenSubsonicBrowser::onArtistsLoaded);
    connect(m_client.get(), &OpenSubsonicClient::artistDetailsReady,
            this, &OpenSubsonicBrowser::onArtistDetailsLoaded);
    connect(m_client.get(), &OpenSubsonicClient::allAlbumsReady,
            this, &OpenSubsonicBrowser::onAllAlbumsLoaded);
    connect(m_client.get(), &OpenSubsonicClient::albumReady,
            this, &OpenSubsonicBrowser::onAlbumLoaded);
    connect(m_client.get(), &OpenSubsonicClient::coverArtReady,
            this, &OpenSubsonicBrowser::onCoverArtReady);
    connect(m_client.get(), &OpenSubsonicClient::connectionStatusChanged,
            this, &OpenSubsonicBrowser::onConnectionStatusChanged);

    if (m_client->ping())
    {
        updateStatus("Connected to server");
        loadArtists();
    }
    else
    {
        updateStatus("Failed to connect to server");
    }
}

OpenSubsonicBrowser::~OpenSubsonicBrowser()
{
}

void OpenSubsonicBrowser::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Top bar with status and refresh button
    QHBoxLayout* topLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Connecting...");
    m_refreshButton = new QPushButton("Refresh");
    connect(m_refreshButton, &QPushButton::clicked, this, &OpenSubsonicBrowser::onRefresh);

    m_modeCombo = new QComboBox();
    m_modeCombo->addItem(tr("Browse by Artist"),    QString("browse"));
    m_modeCombo->addItem(tr("Recently Added"),      QString("newest"));
    m_modeCombo->addItem(tr("Recently Played"),     QString("recent"));
    m_modeCombo->addItem(tr("Most Played"),         QString("frequent"));
    m_modeCombo->addItem(tr("Highest Rated"),       QString("highestRated"));
    m_modeCombo->addItem(tr("Random"),              QString("random"));
    m_modeCombo->addItem(tr("Starred / Favorites"), QString("starred"));
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OpenSubsonicBrowser::onModeChanged);

    topLayout->addWidget(m_modeCombo);
    topLayout->addStretch();
    topLayout->addWidget(m_statusLabel);
    topLayout->addWidget(m_refreshButton);
    mainLayout->addLayout(topLayout);

    // Main content area with splitter
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // --- Artists pane ---
    m_artistPane = new QWidget();
    QVBoxLayout* artistLayout = new QVBoxLayout(m_artistPane);
    m_artistSearch = new QLineEdit();
    m_artistSearch->setPlaceholderText("Search artists...");
    m_artistList = new QListWidget();
    connect(m_artistSearch, &QLineEdit::textChanged, this, &OpenSubsonicBrowser::onArtistSearchChanged);
    connect(m_artistList, &QListWidget::itemClicked,
            this, [this](QListWidgetItem* item) { onArtistSelected(m_artistList->row(item)); });
    artistLayout->addWidget(m_artistSearch);
    artistLayout->addWidget(m_artistList);
    m_artistPane->setLayout(artistLayout);

    // --- Albums pane (with dual view mode) ---
    QWidget* albumPane = new QWidget();
    QVBoxLayout* albumLayout = new QVBoxLayout(albumPane);
    m_albumSearch = new QLineEdit();
    m_albumSearch->setPlaceholderText("Search albums...");
    connect(m_albumSearch, &QLineEdit::textChanged, this, &OpenSubsonicBrowser::onAlbumSearchChanged);
    
    // View mode toggle buttons
    QHBoxLayout* viewToggleLayout = new QHBoxLayout();
    m_albumDetailButton = new QPushButton("Detail");
    m_albumIconButton = new QPushButton("Grid");
    m_albumDetailButton->setCheckable(true);
    m_albumIconButton->setCheckable(true);
    m_albumDetailButton->setChecked(true);
    m_albumDetailButton->setMaximumWidth(80);
    m_albumIconButton->setMaximumWidth(80);
    connect(m_albumDetailButton, &QPushButton::clicked, this, [this]() { switchAlbumViewMode(true); });
    connect(m_albumIconButton, &QPushButton::clicked, this, [this]() { switchAlbumViewMode(false); });
    viewToggleLayout->addWidget(m_albumDetailButton);
    viewToggleLayout->addWidget(m_albumIconButton);
    viewToggleLayout->addStretch();
    
    // Create detail view (QListWidget)
    m_albumListDetail = new QListWidget();
    m_albumListDetail->setItemDelegate(new AlbumDetailDelegate(this));
    connect(m_albumListDetail, &QListWidget::itemClicked,
            this, [this](QListWidgetItem* item) { onAlbumSelected(m_albumListDetail->row(item)); });
    connect(m_albumListDetail, &QListWidget::itemDoubleClicked,
            this, &OpenSubsonicBrowser::onAlbumDoubleClicked);
    
    // Create icon view (QListView)
    m_albumListIcon = new QListView();
    m_albumListIcon->setViewMode(QListView::IconMode);
    m_albumListIcon->setMovement(QListView::Static);
    m_albumListIcon->setResizeMode(QListView::Adjust);
    m_albumListIcon->setUniformItemSizes(true);
    m_albumListIcon->setWrapping(true);
    m_albumListIcon->setSpacing(8);
    m_albumListIcon->setGridSize(QSize(120, 150));
    m_albumListIcon->setIconSize(QSize(100, 100));
    m_albumListIcon->setWordWrap(true);
    m_albumListIcon->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_albumListIcon->setModel(new QStandardItemModel(this));
    m_albumListIcon->setVisible(false);
    connect(m_albumListIcon, &QListView::clicked,
            this, [this](const QModelIndex& idx) { onAlbumSelected(idx.row()); });
    connect(m_albumListIcon, &QListView::doubleClicked,
            this, [this](const QModelIndex& idx) {
                // Simulate double-click on item
                QListWidgetItem* item = m_albumListDetail->item(idx.row());
                if (item) onAlbumDoubleClicked(item);
            });
    
    // Container for swappable views
    m_albumViewContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(m_albumViewContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(m_albumListDetail);
    containerLayout->addWidget(m_albumListIcon);
    m_albumViewContainer->setLayout(containerLayout);
    
    QHBoxLayout* albumButtonLayout = new QHBoxLayout();
    m_openAlbumButton = new QPushButton("Open Album");
    m_enqueueAlbumButton = new QPushButton("Enqueue Album");
    connect(m_openAlbumButton, &QPushButton::clicked, this, &OpenSubsonicBrowser::onOpenAlbum);
    connect(m_enqueueAlbumButton, &QPushButton::clicked, this, &OpenSubsonicBrowser::onEnqueueAlbum);
    albumButtonLayout->addWidget(m_openAlbumButton);
    albumButtonLayout->addWidget(m_enqueueAlbumButton);
    albumButtonLayout->addStretch();
    
    albumLayout->addWidget(m_albumSearch);
    albumLayout->addLayout(viewToggleLayout);
    albumLayout->addWidget(m_albumViewContainer);
    albumLayout->addLayout(albumButtonLayout);
    albumPane->setLayout(albumLayout);

    // --- Tracks pane ---
    QWidget* trackPane = new QWidget();
    QVBoxLayout* trackLayout = new QVBoxLayout(trackPane);
    m_trackSearch = new QLineEdit();
    m_trackSearch->setPlaceholderText("Search tracks...");
    m_trackList = new QListWidget();
    m_trackList->setSelectionMode(QAbstractItemView::MultiSelection);
    connect(m_trackSearch, &QLineEdit::textChanged, this, &OpenSubsonicBrowser::onTrackSearchChanged);
    connect(m_trackList, &QListWidget::itemDoubleClicked,
            this, &OpenSubsonicBrowser::onTrackDoubleClicked);
    
    QHBoxLayout* trackButtonLayout = new QHBoxLayout();
    m_openTracksButton = new QPushButton("Open");
    m_enqueueTracksButton = new QPushButton("Enqueue");
    connect(m_openTracksButton, &QPushButton::clicked, this, &OpenSubsonicBrowser::onOpenTracks);
    connect(m_enqueueTracksButton, &QPushButton::clicked, this, &OpenSubsonicBrowser::onEnqueueTracks);
    trackButtonLayout->addWidget(m_openTracksButton);
    trackButtonLayout->addWidget(m_enqueueTracksButton);
    trackButtonLayout->addStretch();
    
    trackLayout->addWidget(m_trackSearch);
    trackLayout->addWidget(m_trackList);
    trackLayout->addLayout(trackButtonLayout);
    trackPane->setLayout(trackLayout);

    splitter->addWidget(m_artistPane);
    splitter->addWidget(albumPane);
    splitter->addWidget(trackPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);

    mainLayout->addWidget(splitter);
    setLayout(mainLayout);
}

void OpenSubsonicBrowser::loadArtists()
{
    m_artistList->clear();
    m_albumListDetail->clear();
    m_albumListIcon->clearSelection();
    m_trackList->clear();
    updateStatus("Loading artists...");

    if (m_client)
        m_client->getArtists();
}

void OpenSubsonicBrowser::onArtistsLoaded(const QJsonArray& artists)
{
    m_allArtists = artists;
    m_artistSearch->clear();
    filterArtistList("");
    populateAlbumList(QJsonArray());
    m_trackList->clear();
    updateStatus(QString("Loaded %1 artists").arg(artists.size()));
}

void OpenSubsonicBrowser::onArtistSelected(int row)
{
    if (row < 0 || row >= m_artistList->count())
        return;

    if (!m_client)
        return;

    QListWidgetItem* item = m_artistList->item(row);
    QString artistId = item->data(Qt::UserRole).toString();

    populateAlbumList(QJsonArray());
    m_trackList->clear();

    if (artistId.isEmpty())
    {
        updateStatus(tr("Loading all albums..."));
        m_client->getAllAlbums();
        return;
    }

    updateStatus(QString("Loading albums for %1...").arg(item->text()));
    m_client->getArtist(artistId);
}

void OpenSubsonicBrowser::onArtistDetailsLoaded(const QJsonArray& albums)
{
    m_allAlbums = albums;
    m_albumSearch->clear();
    filterAlbumList("");
    m_trackList->clear();
    updateStatus(QString("Loaded %1 albums").arg(albums.size()));
}

void OpenSubsonicBrowser::onAllAlbumsLoaded(const QJsonArray& albums)
{
    m_allAlbums = albums;
    m_albumSearch->clear();
    filterAlbumList("");
    m_trackList->clear();
    updateStatus(QString("Loaded %1 albums").arg(albums.size()));
}

void OpenSubsonicBrowser::onAlbumSelected(int row)
{
    if (row < 0 || row >= m_albumListDetail->count())
        return;

    if (!m_client)
        return;

    QListWidgetItem* item = m_albumListDetail->item(row);
    QString albumId = item->data(Qt::UserRole).toString();

    m_trackList->clear();
    updateStatus(QString("Loading tracks for %1...").arg(item->text()));
    
    m_client->getAlbum(albumId);
}

void OpenSubsonicBrowser::onAlbumDoubleClicked(QListWidgetItem* item)
{
    if (!item || !m_client)
        return;

    // First load the tracks, then enqueue them all
    QString albumId = item->data(Qt::UserRole).toString();
    QString albumName = item->text();

    connect(m_client.get(), &OpenSubsonicClient::albumReady,
        this, [this, albumName](const QJsonArray& songs) {
            Index<PlaylistAddItem> items;
            for (const QJsonValue& songVal : songs)
            {
                QJsonObject song = songVal.toObject();
                QString trackId = song.value("id").toString();
                items.append(String(m_client->getStreamUrl(trackId).toUtf8().constData()));
            }
            if (items.len() > 0)
            {
                aud_drct_pl_open_list(std::move(items));
                updateStatus(QString("Added %1 tracks from \"%2\" to playlist").arg(songs.size()).arg(albumName));
            }
        }, Qt::SingleShotConnection);

    m_trackList->clear();
    updateStatus(QString("Loading \"%1\"...").arg(albumName));
    m_client->getAlbum(albumId);
}

void OpenSubsonicBrowser::onAlbumLoaded(const QJsonArray& songs)
{
    m_allTracks = songs;
    m_trackSearch->clear();
    filterTrackList("");
    updateStatus(QString("Loaded %1 tracks").arg(songs.size()));
}

void OpenSubsonicBrowser::onConnectionStatusChanged(bool connected)
{
    if (connected)
        updateStatus("Connected to server");
}

void OpenSubsonicBrowser::onCoverArtReady(const QString& albumId, const QByteArray& imageData)
{
    // Load pixmap from image data
    QPixmap coverPixmap;
    if (!imageData.isEmpty())
        coverPixmap.loadFromData(imageData);
    
    if (coverPixmap.isNull())
        return;
    
    // Update detail view item
    for (int i = 0; i < m_albumListDetail->count(); ++i)
    {
        QListWidgetItem* item = m_albumListDetail->item(i);
        if (item && item->data(Qt::UserRole).toString() == albumId)
        {
            // Scale to 40px height for thumbnail
            QPixmap thumbPixmap = coverPixmap.scaledToHeight(40, Qt::SmoothTransformation);
            item->setData(Qt::UserRole + 3, thumbPixmap);  // Store pixmap in custom role
            break;
        }
    }
    
    // Update icon view item
    QStandardItemModel* iconModel = qobject_cast<QStandardItemModel*>(m_albumListIcon->model());
    if (iconModel)
    {
        for (int i = 0; i < iconModel->rowCount(); ++i)
        {
            QStandardItem* item = iconModel->item(i);
            if (item && item->data(Qt::UserRole).toString() == albumId)
            {
                // Scale to 100px width for grid view
                QPixmap scaled = coverPixmap.scaledToWidth(100, Qt::SmoothTransformation);
                item->setIcon(QIcon(scaled));
                break;
            }
        }
    }
}

void OpenSubsonicBrowser::onRefresh()
{
    // Re-trigger the current mode
    onModeChanged(m_modeCombo->currentIndex());
}

void OpenSubsonicBrowser::onModeChanged(int index)
{
    if (!m_client)
        return;

    QString type = m_modeCombo->itemData(index).toString();
    bool browseMode = (type == "browse");

    m_artistPane->setVisible(browseMode);
    populateAlbumList(QJsonArray());
    m_trackList->clear();

    if (browseMode)
    {
        loadArtists();
    }
    else
    {
        updateStatus(tr("Loading %1...").arg(m_modeCombo->currentText()));
        m_client->getAlbumList(type);
    }
}

void OpenSubsonicBrowser::onTrackDoubleClicked(QListWidgetItem* item)
{
    if (!item || !m_client)
        return;

    QString trackId = item->data(Qt::UserRole).toString();
    QString streamUrl = m_client->getStreamUrl(trackId);

    // Add to playlist and play
    Index<PlaylistAddItem> items;
    items.append(String(streamUrl.toUtf8().constData()));
    aud_drct_pl_open_list(std::move(items));
}

void OpenSubsonicBrowser::onArtistSearchChanged(const QString& text)
{
    filterArtistList(text);
}

void OpenSubsonicBrowser::onAlbumSearchChanged(const QString& text)
{
    filterAlbumList(text);
}

void OpenSubsonicBrowser::onTrackSearchChanged(const QString& text)
{
    filterTrackList(text);
}

void OpenSubsonicBrowser::filterArtistList(const QString& filter)
{
    m_artistList->clear();

    // "All" always appears at top regardless of filter
    QListWidgetItem* allItem = new QListWidgetItem(tr("All artists"));
    allItem->setData(Qt::UserRole, QString());  // empty id signals "all"
    QFont f = allItem->font();
    f.setItalic(true);
    allItem->setFont(f);
    m_artistList->addItem(allItem);

    QString lowerFilter = filter.toLower();
    for (const QJsonValue& artistVal : m_allArtists)
    {
        QJsonObject artist = artistVal.toObject();
        QString name = artist.value("name").toString();
        QString id = artist.value("id").toString();
        if (name.toLower().contains(lowerFilter))
        {
            QListWidgetItem* item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, id);
            m_artistList->addItem(item);
        }
    }
}

void OpenSubsonicBrowser::filterAlbumList(const QString& filter)
{
    QString lowerFilter = filter.toLower();
    QJsonArray filtered;
    
    for (const QJsonValue& albumVal : m_allAlbums)
    {
        QJsonObject album = albumVal.toObject();
        QString name = album.value("name").toString();
        
        if (name.toLower().contains(lowerFilter))
        {
            filtered.append(albumVal);
        }
    }
    
    populateAlbumList(filtered);
}

void OpenSubsonicBrowser::filterTrackList(const QString& filter)
{
    m_trackList->clear();
    QString lowerFilter = filter.toLower();
    
    for (const QJsonValue& songVal : m_allTracks)
    {
        QJsonObject song = songVal.toObject();
        QString title = song.value("title").toString();
        QString id = song.value("id").toString();
        
        if (title.toLower().contains(lowerFilter))
        {
            QListWidgetItem* item = new QListWidgetItem(title);
            item->setData(Qt::UserRole, id);
            m_trackList->addItem(item);
        }
    }
}

void OpenSubsonicBrowser::onOpenAlbum()
{
    if (!m_client)
        return;

    QListWidgetItem* selected = getCurrentAlbumItem();
    if (!selected)
    {
        updateStatus("Please select an album");
        return;
    }

    QString albumId = selected->data(Qt::UserRole).toString();
    QString albumName = selected->text();

    connect(m_client.get(), &OpenSubsonicClient::albumReady,
        this, [this, albumName](const QJsonArray& songs) {
            addTracksToPlaylist(songs, false);
            updateStatus(QString("Playing \"%1\"").arg(albumName));
        }, Qt::SingleShotConnection);

    updateStatus(QString("Loading \"%1\"...").arg(albumName));
    m_client->getAlbum(albumId);
}

void OpenSubsonicBrowser::onEnqueueAlbum()
{
    if (!m_client)
        return;

    QListWidgetItem* selected = getCurrentAlbumItem();
    if (!selected)
    {
        updateStatus("Please select an album");
        return;
    }

    QString albumId = selected->data(Qt::UserRole).toString();
    QString albumName = selected->text();

    connect(m_client.get(), &OpenSubsonicClient::albumReady,
        this, [this, albumName](const QJsonArray& songs) {
            addTracksToPlaylist(songs, true);
            updateStatus(QString("Queued %1 tracks from \"%2\"").arg(songs.size()).arg(albumName));
        }, Qt::SingleShotConnection);

    updateStatus(QString("Loading \"%1\"...").arg(albumName));
    m_client->getAlbum(albumId);
}

void OpenSubsonicBrowser::onOpenTracks()
{
    QList<QListWidgetItem*> selected = m_trackList->selectedItems();
    if (selected.isEmpty())
    {
        updateStatus("Please select one or more tracks");
        return;
    }

    addTracksToPlaylist(convertSelectedToArray(selected), false);
    updateStatus(QString("Playing %1 track(s)").arg(selected.size()));
}

void OpenSubsonicBrowser::onEnqueueTracks()
{
    QList<QListWidgetItem*> selected = m_trackList->selectedItems();
    if (selected.isEmpty())
    {
        updateStatus("Please select one or more tracks");
        return;
    }

    addTracksToPlaylist(convertSelectedToArray(selected), true);
    updateStatus(QString("Queued %1 track(s)").arg(selected.size()));
}

void OpenSubsonicBrowser::addTracksToPlaylist(const QJsonArray& songs, bool enqueue)
{
    if (!m_client)
        return;

    Index<PlaylistAddItem> items;
    for (const QJsonValue& songVal : songs)
    {
        QJsonObject song = songVal.toObject();
        QString trackId = song.value("id").toString();
        QString streamUrl = m_client->getStreamUrl(trackId);
        items.append(String(streamUrl.toUtf8().constData()));
    }

    if (items.len() > 0)
    {
        if (enqueue)
            aud_drct_pl_add_list(std::move(items), -1);
        else
            aud_drct_pl_open_list(std::move(items));
    }
}

QJsonArray OpenSubsonicBrowser::convertSelectedToArray(const QList<QListWidgetItem*>& items)
{
    QJsonArray array;
    for (QListWidgetItem* item : items)
    {
        // Find the corresponding object from m_allTracks
        QString trackId = item->data(Qt::UserRole).toString();
        for (const QJsonValue& songVal : m_allTracks)
        {
            if (songVal.toObject().value("id").toString() == trackId)
            {
                array.append(songVal);
                break;
            }
        }
    }
    return array;
}

void OpenSubsonicBrowser::switchAlbumViewMode(bool detailMode)
{
    m_albumDetailMode = detailMode;
    m_albumDetailButton->setChecked(detailMode);
    m_albumIconButton->setChecked(!detailMode);
    m_albumListDetail->setVisible(detailMode);
    m_albumListIcon->setVisible(!detailMode);
}

void OpenSubsonicBrowser::populateAlbumList(const QJsonArray& albums)
{
    m_albumListDetail->clear();
    
    QStandardItemModel* iconModel = qobject_cast<QStandardItemModel*>(m_albumListIcon->model());
    if (iconModel)
        iconModel->clear();

    // Reset view geometry after clear() invalidates the layout
    m_albumListIcon->setGridSize(QSize(120, 150));
    m_albumListIcon->setIconSize(QSize(100, 100));
    
    for (const QJsonValue& albumVal : albums)
    {
        QJsonObject album = albumVal.toObject();
        QString name = album.value("name").toString();
        QString id = album.value("id").toString();
        
        // Create list widget item for detail view
        QListWidgetItem* item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, id);
        
        // Store metadata for detail view delegate
        int songCount = album.value("songCount").toInt();
        int duration = album.value("duration").toInt();
        QString trackCount = QString("Tracks: %1").arg(songCount);
        int minutes = duration / 60;
        QString durationStr = QString("Duration: %1:%2").arg(minutes).arg(duration % 60, 2, 10, QChar('0'));
        
        item->setData(Qt::UserRole + 1, trackCount);
        item->setData(Qt::UserRole + 2, durationStr);
        
        m_albumListDetail->addItem(item);
        
        // Create icon view item
        if (iconModel)
        {
            QStandardItem* iconItem = new QStandardItem(name);
            iconItem->setData(id, Qt::UserRole);
            iconItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            // Placeholder ensures uniform cell size is computed correctly before async art arrives
            QPixmap placeholder(100, 100);
            placeholder.fill(Qt::transparent);
            iconItem->setIcon(QIcon(placeholder));
            iconModel->appendRow(iconItem);
        }
        
        // Start async cover art fetch
        if (m_client)
        {
            QString coverArtId = album.value("coverArt").toString();
            if (coverArtId.isEmpty())
                coverArtId = id;
            
            if (!coverArtId.isEmpty())
            {
                m_client->getCoverArtAsync(coverArtId, id);
            }
        }
    }

    m_albumListIcon->doItemsLayout();
}

void OpenSubsonicBrowser::updateStatus(const QString& message)
{
    m_statusLabel->setText(message);
}

QListWidgetItem* OpenSubsonicBrowser::getCurrentAlbumItem()
{
    if (m_albumDetailMode)
        return m_albumListDetail->currentItem();
    else
    {
        QModelIndex idx = m_albumListIcon->currentIndex();
        if (!idx.isValid())
            return nullptr;
        // Need to get corresponding item from detail list
        return m_albumListDetail->item(idx.row());
    }
}

int OpenSubsonicBrowser::getCurrentAlbumRow()
{
    if (m_albumDetailMode)
        return m_albumListDetail->currentRow();
    else
        return m_albumListIcon->currentIndex().row();
}
