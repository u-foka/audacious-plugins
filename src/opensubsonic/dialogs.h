#ifndef OPENSUBSONIC_PLUGIN_DIALOGS_H
#define OPENSUBSONIC_PLUGIN_DIALOGS_H

#include <QDialog>
#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>

class OpenSubsonicBrowser;

// Embedded prefs widget for Audacious settings (WidgetCustomQt)
class OpenSubsonicPrefsWidget : public QWidget
{
    Q_OBJECT

public:
    OpenSubsonicPrefsWidget(QWidget* parent = nullptr);

    void save();

private slots:
    void onTestConnection();
    void onBrowse();

private:
    QLineEdit* m_serverUrlEdit;
    QLineEdit* m_usernameEdit;
    QLineEdit* m_passwordEdit;
    QCheckBox* m_scrobbleEnabledCheck;
    QSpinBox*  m_scrobbleThresholdSpin;
};

// Standalone preferences dialog (used internally if needed)
class OpenSubsonicPrefsWindow : public QDialog
{
    Q_OBJECT

public:
    OpenSubsonicPrefsWindow(QWidget* parent = nullptr);

private:
    OpenSubsonicPrefsWidget* m_widget;
};

class OpenSubsonicBrowserWindow : public QDialog
{
    Q_OBJECT

public:
    OpenSubsonicBrowserWindow(QWidget* parent = nullptr);

private:
    OpenSubsonicBrowser* m_browser;
};

// Factory function called by Audacious WidgetCustomQt
void * opensubsonic_create_prefs_widget();
// Called by Audacious when settings are applied
void opensubsonic_prefs_apply();

#endif // OPENSUBSONIC_PLUGIN_DIALOGS_H
