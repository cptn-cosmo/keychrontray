#pragma once

#include "ledstate.h"

#include <QObject>
#include <QString>
#include <optional>

class KeyboardBackend;
class ProfileManager;

// Decides which lighting is active and pushes it to the keyboard.
//
// Precedence: live preview > manual selection > detected game > default profile.
// A manual selection stays until the detected game changes (game start/exit) or "Restore default".
//
// The keyboard's lighting at first connection is captured as the "keyboard" pseudo-profile and restored
// on exit. It is persisted with a dirty flag so a crash while a profile is applied does not make the game
// colors the new baseline.
class LightingController : public QObject
{
    Q_OBJECT
public:
    static inline const QString KeyboardProfileId = QStringLiteral("keyboard");

    // statePath defaults to ~/.local/state/keychrontray/original.json
    LightingController(KeyboardBackend *backend, ProfileManager *profiles, const QString &statePath = QString(), QObject *parent = nullptr);

    void setDefaultProfile(const QString &id);
    QString defaultProfile() const { return m_defaultProfile; }

    bool setManualProfile(const QString &id);
    void restoreDefault(); // clears manual override
    // Called by the game detector. Empty id = no game running.
    void setGameProfile(const QString &id, const QString &gameLabel);

    void preview(const LedState &state);
    void endPreview();

    QString activeProfileId() const;
    QString activeReason() const; // "manual", "game: <name>", "default", "preview"
    QString gameLabel() const { return m_gameLabel; }
    std::optional<LedState> originalState() const { return m_original; }

    // Writes the currently applied lighting to EEPROM (explicit user action only).
    bool saveToKeyboard();
    // Restores the captured original lighting (call on quit).
    void restoreOriginal();

public Q_SLOTS:
    void onConnectedChanged(bool connected);
    void reapply(bool force = false);

Q_SIGNALS:
    void activeChanged(const QString &profileId, const QString &reason);
    void applyFailed(const QString &message);

private:
    std::optional<LedState> resolve(const QString &id) const;
    void captureOriginal();
    void persist(bool dirty);

    KeyboardBackend *m_backend;
    ProfileManager *m_profiles;
    QString m_statePath;
    QString m_defaultProfile = KeyboardProfileId;
    QString m_manual;
    QString m_game;
    QString m_gameLabel;
    std::optional<LedState> m_preview;
    std::optional<LedState> m_original;
    std::optional<LedState> m_lastApplied;
    QString m_lastActive;
};
