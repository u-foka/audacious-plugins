#include "dialogs.h"
#include "browser.h"
#include "settings.h"
#include "api.h"
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

// ── OpenSubsonicPrefsWidget ───────────────────────────────────────────────────

static OpenSubsonicPrefsWidget * s_prefsWidget = nullptr;

OpenSubsonicPrefsWidget::OpenSubsonicPrefsWidget(QWidget * parent) : QWidget(parent)
{
    QVBoxLayout * outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QFormLayout * form = new QFormLayout();

    m_serverUrlEdit = new QLineEdit();
    m_serverUrlEdit->setPlaceholderText("http://yourserver:4533");
    form->addRow(tr("Server URL:"), m_serverUrlEdit);

    m_usernameEdit = new QLineEdit();
    form->addRow(tr("Username:"), m_usernameEdit);

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password:"), m_passwordEdit);

    m_scrobbleEnabledCheck = new QCheckBox(tr("Report playback history to server"));
    form->addRow(QString(), m_scrobbleEnabledCheck);

    m_scrobbleThresholdSpin = new QSpinBox();
    m_scrobbleThresholdSpin->setRange(1, 100);
    m_scrobbleThresholdSpin->setSuffix("%");
    m_scrobbleThresholdSpin->setToolTip(tr("Percentage of track that must play before reporting"));
    form->addRow(tr("Report after:"), m_scrobbleThresholdSpin);
    // Threshold only makes sense when scrobbling is on
    connect(m_scrobbleEnabledCheck, &QCheckBox::toggled,
            m_scrobbleThresholdSpin, &QSpinBox::setEnabled);

    outer->addLayout(form);

    QHBoxLayout * buttons = new QHBoxLayout();
    QPushButton * testButton   = new QPushButton(tr("Test Connection"));
    QPushButton * browseButton = new QPushButton(tr("Browse Library..."));
    connect(testButton,   &QPushButton::clicked, this, &OpenSubsonicPrefsWidget::onTestConnection);
    connect(browseButton, &QPushButton::clicked, this, &OpenSubsonicPrefsWidget::onBrowse);
    buttons->addWidget(testButton);
    buttons->addWidget(browseButton);
    buttons->addStretch();
    outer->addLayout(buttons);

    // Load current settings
    m_serverUrlEdit->setText(OpenSubsonicSettings::getServerUrl());
    m_usernameEdit->setText(OpenSubsonicSettings::getUsername());
    // Password is stored as a hash; leave field empty but hint that one is set
    if (!OpenSubsonicSettings::getPasswordToken().isEmpty())
        m_passwordEdit->setPlaceholderText(tr("(password set — enter new password to change)"));
    m_scrobbleEnabledCheck->setChecked(OpenSubsonicSettings::isScrobbleEnabled());
    m_scrobbleThresholdSpin->setValue(OpenSubsonicSettings::getScrobbleThreshold());
    m_scrobbleThresholdSpin->setEnabled(m_scrobbleEnabledCheck->isChecked());

    s_prefsWidget = this;
}

void OpenSubsonicPrefsWidget::save()
{
    OpenSubsonicSettings::setServerUrl(m_serverUrlEdit->text().trimmed());
    OpenSubsonicSettings::setUsername(m_usernameEdit->text().trimmed());
    if (!m_passwordEdit->text().isEmpty())
        OpenSubsonicSettings::setPassword(m_passwordEdit->text());
    OpenSubsonicSettings::setEnabled(!m_serverUrlEdit->text().trimmed().isEmpty());
    OpenSubsonicSettings::setScrobbleEnabled(m_scrobbleEnabledCheck->isChecked());
    OpenSubsonicSettings::setScrobbleThreshold(m_scrobbleThresholdSpin->value());
}

void OpenSubsonicPrefsWidget::onTestConnection()
{
    save();

    QString url  = m_serverUrlEdit->text().trimmed();
    QString user = m_usernameEdit->text().trimmed();
    QString plaintext = m_passwordEdit->text();
    QString token, salt;
    if (!plaintext.isEmpty())
        OpenSubsonicSettings::computeTokenAndSalt(plaintext, token, salt);
    else {
        token = OpenSubsonicSettings::getPasswordToken();
        salt  = OpenSubsonicSettings::getPasswordSalt();
    }

    if (url.isEmpty() || user.isEmpty())
    {
        QMessageBox::warning(this, tr("OpenSubsonic"), tr("Please fill in Server URL and Username."));
        return;
    }

    OpenSubsonicClient client(url, user, token, salt);
    if (client.ping())
        QMessageBox::information(this, tr("OpenSubsonic"), tr("Connected successfully!"));
    else
        QMessageBox::critical(this, tr("OpenSubsonic"), tr("Connection failed. Check the URL and credentials."));
}

void OpenSubsonicPrefsWidget::onBrowse()
{
    save();
    auto * win = new OpenSubsonicBrowserWindow(this);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->show();
}

// ── Factory functions called by Audacious WidgetCustomQt / PluginPreferences ──

void * opensubsonic_create_prefs_widget()
{
    return new OpenSubsonicPrefsWidget();
}

void opensubsonic_prefs_apply()
{
    if (s_prefsWidget)
        s_prefsWidget->save();
}

// ── OpenSubsonicPrefsWindow ───────────────────────────────────────────────────

OpenSubsonicPrefsWindow::OpenSubsonicPrefsWindow(QWidget * parent) : QDialog(parent)
{
    setWindowTitle(tr("OpenSubsonic Settings"));
    setMinimumWidth(420);

    QVBoxLayout * layout = new QVBoxLayout(this);
    m_widget = new OpenSubsonicPrefsWidget(this);
    layout->addWidget(m_widget);

    QHBoxLayout * btns = new QHBoxLayout();
    QPushButton * ok     = new QPushButton(tr("OK"));
    QPushButton * cancel = new QPushButton(tr("Cancel"));
    connect(ok,     &QPushButton::clicked, this, [this]{ m_widget->save(); accept(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);
    layout->addLayout(btns);
}

// ── OpenSubsonicBrowserWindow ─────────────────────────────────────────────────

OpenSubsonicBrowserWindow::OpenSubsonicBrowserWindow(QWidget * parent) : QDialog(parent)
{
    setWindowTitle(tr("Browse OpenSubsonic"));
    QVBoxLayout * layout = new QVBoxLayout(this);
    m_browser = new OpenSubsonicBrowser(this);
    layout->addWidget(m_browser);
    resize(900, 600);
}
