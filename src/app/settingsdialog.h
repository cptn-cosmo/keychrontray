#pragma once

#include "profile.h"

#include <QDialog>

class TrayController;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(TrayController *tray, QWidget *parent = nullptr);
    ~SettingsDialog() override;

protected:
    void done(int r) override;

private:
    QWidget *buildProfilesTab();
    QWidget *buildGamesTab();
    QWidget *buildGeneralTab();
    QWidget *buildDiagnosticsTab();

    // Profiles
    void reloadProfileList(const QString &select = QString());
    void loadProfileIntoEditor(const QString &id);
    void editorChanged();
    void updateColorButton();
    void rebuildStopButtons();
    void updateGradientPreview();
    void updateStyleRows();
    bool saveCurrentProfile();
    bool confirmDiscard();

    // Games
    void loadGames();
    void addGameRow(const QString &id, const QString &name, const QString &appIds, const QString &procs, const QString &profile, int priority);
    void fillProfileCombo(QComboBox *c, const QString &selected, bool allowNone, bool allowKeyboard);
    void saveGames();
    void addFromSteam();

    // General
    void loadGeneral();
    void saveGeneral();

    void refreshDiagnostics();

    TrayController *m_tray;

    QListWidget *m_profileList = nullptr;
    QLabel *m_profileErrors = nullptr;
    QLabel *m_idLabel = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_group = nullptr;
    QLineEdit *m_description = nullptr;
    QComboBox *m_effect = nullptr;
    QPushButton *m_colorButton = nullptr;
    QSlider *m_brightness = nullptr;
    QSlider *m_speed = nullptr;
    QLabel *m_brightnessLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QCheckBox *m_livePreview = nullptr;
    QFormLayout *m_form = nullptr;
    QComboBox *m_style = nullptr;
    QWidget *m_stopsWidget = nullptr;
    QHBoxLayout *m_stopsLayout = nullptr;
    QComboBox *m_motion = nullptr;
    QDoubleSpinBox *m_cycle = nullptr;
    QLabel *m_gradientPreview = nullptr;
    QLabel *m_styleNote = nullptr;
    QPushButton *m_saveProfile = nullptr;
    Profile m_edit;
    bool m_dirty = false;
    bool m_loading = false;

    QTableWidget *m_games = nullptr;
    QComboBox *m_defaultProfile = nullptr;
    QComboBox *m_fallbackProfile = nullptr;
    QLineEdit *m_ignored = nullptr;
    QLabel *m_gamesStatus = nullptr;

    QCheckBox *m_autoSwitch = nullptr;
    QSpinBox *m_scanInterval = nullptr;
    QLineEdit *m_thresholds = nullptr;
    QCheckBox *m_autostart = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_steamId = nullptr;

    QLabel *m_diag = nullptr;
    QPlainTextEdit *m_log = nullptr;
};
