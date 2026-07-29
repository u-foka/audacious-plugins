#include <memory>
#include <cstdlib>

#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>
#include <libaudcore/i18n.h>
#include <libaudcore/interface.h>
#include <libaudcore/hook.h>
#include <libaudcore/drct.h>
#include <libaudcore/playlist.h>

#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QDateTime>

#include "api.h"
#include "dialogs.h"
#include "settings.h"

#define PLUGIN_DOMAIN "opensubsonic-plugin"

static const PreferencesWidget prefs_widgets[] = {
    WidgetCustomQt(opensubsonic_create_prefs_widget)
};

static const PluginPreferences plugin_prefs = {
    {prefs_widgets},
    nullptr,                       // init
    opensubsonic_prefs_apply,      // apply
    nullptr                        // cleanup
};

class OpenSubsonicPlugin : public GeneralPlugin
{
public:
    static const char about[];

    static constexpr PluginInfo pluginInfo = {
        N_("OpenSubsonic Browser"),
        PLUGIN_DOMAIN,
        about,
        & plugin_prefs,
        PluginQtOnly
    };

    constexpr OpenSubsonicPlugin() : GeneralPlugin(pluginInfo, false) {}

    bool init() override;
    void cleanup() override;
};

const char OpenSubsonicPlugin::about[] =
    N_("OpenSubsonic Browser for Audacious\n\n"
       "Browse and play music from OpenSubsonic-compatible servers.\n\n"
       "License: GNU GPLv2+");

static std::unique_ptr<OpenSubsonicBrowserWindow> s_browserWindow;
static std::unique_ptr<OpenSubsonicClient> s_scrobbleClient;
static QTimer* s_reportTimer = nullptr;   // state-change + seek detection
static QString s_currentTrackId;
static bool s_scrobbled = false;
static bool s_wasPaused = false;
static int  s_lastReportedPositionMs = 0; // position at last reportPlayback call
static qint64 s_lastReportWallMs = 0;     // wall-clock time of last reportPlayback call

static QString currentTrackId()
{
    auto playlist = Playlist::playing_playlist();
    if (!playlist.exists())
        return {};

    int pos = playlist.get_position();
    if (pos < 0)
        return {};

    String uri = playlist.entry_filename(pos);
    if (!uri)
        return {};

    QString uriStr = QString(uri);
    QUrl parsedUrl(uriStr);
    return QUrlQuery(parsedUrl).queryItemValue("id");
}

static OpenSubsonicClient* reportClient()
{
    if (!OpenSubsonicSettings::isScrobbleEnabled())
        return nullptr;

    QString url  = OpenSubsonicSettings::getServerUrl();
    QString user = OpenSubsonicSettings::getUsername();
    QString token = OpenSubsonicSettings::getPasswordToken();
    QString salt  = OpenSubsonicSettings::getPasswordSalt();
    if (url.isEmpty() || user.isEmpty())
        return nullptr;

    if (!s_scrobbleClient)
        s_scrobbleClient = std::make_unique<OpenSubsonicClient>(url, user, token, salt);

    return s_scrobbleClient.get();
}

using PS = OpenSubsonicClient::PlaybackState;

static void checkScrobblePosition()
{
    if (s_scrobbled || s_currentTrackId.isEmpty())
        return;

    int duration = aud_drct_get_length();
    int position = aud_drct_get_time();

    if (duration <= 0)
        return;

    int threshold = OpenSubsonicSettings::getScrobbleThreshold();
    if (position * 100 / duration >= threshold)
    {
        if (auto* client = reportClient())
        {
            client->scrobble(s_currentTrackId);
            s_scrobbled = true;
        }
    }
}

static void onPlaybackBegin(void*, void*)
{
    s_currentTrackId = currentTrackId();
    s_scrobbled = false;
    s_wasPaused = false;
    s_lastReportedPositionMs = 0;
    s_lastReportWallMs = QDateTime::currentMSecsSinceEpoch();

    if (s_currentTrackId.isEmpty())
        return;

    if (auto* client = reportClient())
    {
        // "starting" tells the server to open a new playback session
        client->reportPlayback(s_currentTrackId, 0, PS::Starting);
        client->reportPlayback(s_currentTrackId, 0, PS::Playing);
        s_lastReportedPositionMs = 0;
        s_lastReportWallMs = QDateTime::currentMSecsSinceEpoch();
    }

    if (s_reportTimer)
        s_reportTimer->start();
}

static void onPlaybackEnd(void*, void*)
{
    if (s_reportTimer)
        s_reportTimer->stop();

    if (!s_currentTrackId.isEmpty())
        if (auto* client = reportClient())
            client->reportPlayback(s_currentTrackId, aud_drct_get_time(), PS::Stopped);

    s_currentTrackId.clear();
    s_scrobbled = false;
    s_wasPaused = false;
    s_lastReportedPositionMs = 0;
    s_lastReportWallMs = 0;
}

static void openBrowser()
{
    if (!s_browserWindow)
        s_browserWindow = std::make_unique<OpenSubsonicBrowserWindow>();
    s_browserWindow->show();
    s_browserWindow->raise();
    s_browserWindow->activateWindow();
}

bool OpenSubsonicPlugin::init()
{
    aud_plugin_menu_add(AudMenuID::Main, openBrowser, N_("Browse OpenSubsonic"), nullptr);
    hook_associate("playback begin", onPlaybackBegin, nullptr);
    hook_associate("playback end", onPlaybackEnd, nullptr);
    hook_associate("playback stop", onPlaybackEnd, nullptr);

    // Poll every 2 seconds: detect pause/resume and seeks
    s_reportTimer = new QTimer();
    s_reportTimer->setInterval(2000);
    QObject::connect(s_reportTimer, &QTimer::timeout, []() {
        if (s_currentTrackId.isEmpty())
            return;

        checkScrobblePosition();

        bool paused = aud_drct_get_paused();
        int posMs = aud_drct_get_time();
        auto* client = reportClient();

        if (!s_wasPaused && paused)
        {
            if (client)
                client->reportPlayback(s_currentTrackId, posMs, PS::Paused);
        }
        else if (s_wasPaused && !paused)
        {
            if (client)
                client->reportPlayback(s_currentTrackId, posMs, PS::Playing);
            s_lastReportedPositionMs = posMs;
            s_lastReportWallMs = QDateTime::currentMSecsSinceEpoch();
        }
        else if (!paused && client && s_lastReportWallMs > 0)
        {
            // Seek detection: compare actual position to where we'd expect to be
            qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - s_lastReportWallMs;
            int expectedPos = s_lastReportedPositionMs + static_cast<int>(elapsedMs);
            if (std::abs(posMs - expectedPos) > 3000)
            {
                client->reportPlayback(s_currentTrackId, posMs, PS::Playing);
                s_lastReportedPositionMs = posMs;
                s_lastReportWallMs = QDateTime::currentMSecsSinceEpoch();
            }
        }

        s_wasPaused = paused;
    });

    return true;
}

void OpenSubsonicPlugin::cleanup()
{
    hook_dissociate("playback begin", onPlaybackBegin);
    hook_dissociate("playback end", onPlaybackEnd);
    hook_dissociate("playback stop", onPlaybackEnd);

    delete s_reportTimer;
    s_reportTimer = nullptr;

    // Send stopped before the client is destroyed
    if (!s_currentTrackId.isEmpty())
        if (auto* client = reportClient())
            client->reportPlaybackSync(s_currentTrackId, aud_drct_get_time(), PS::Stopped);

    aud_plugin_menu_remove(AudMenuID::Main, openBrowser);
    s_scrobbleClient.reset();
    s_browserWindow.reset();
}

EXPORT OpenSubsonicPlugin aud_plugin_instance;

