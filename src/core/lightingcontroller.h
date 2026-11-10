#pragma once

#include "gradient.h"
#include "ledstate.h"

#include <QObject>
#include <QString>
#include <optional>

class GradientAnimator;
class KeyboardBackend;
class ProfileManager;
struct Profile;

// What the keyboard should show: the global VIA state plus, optionally, a per-key gradient.
struct LightingScene {
    LedState base;
    std::optional<Gradient> gradient;
    bool operator==(const LightingScene &o) const { return base == o.base && gradient == o.gradient; }

    static LightingScene fromProfile(const Profile &p);
};

// Decides which lighting is active and pushes it to the keyboard.
//
// Precedence: live preview > manual selection > detected game > default profile.
// A manual selection stays until the detected game changes (game start/exit) or "Restore default".
//
// The keyboard's lighting at first connection (global state and per-key table) is captured as the
// "keyboard" pseudo-profile and restored on exit. It is persisted with a dirty flag so a crash while a
// profile is applied does not make the game colors the new baseline.
//
// Gradients are written to the per-key table (RAM) and shown with the per-key effect. Keyboards without
// per-key support get the gradient's first color as a solid color.
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

    void preview(const LightingScene &scene);
    void preview(const LedState &state) { preview(LightingScene{state, std::nullopt}); }
    void endPreview();

    QString activeProfileId() const;
    QString activeReason() const; // "manual", "game: <name>", "default", "preview"
    QString gameLabel() const { return m_gameLabel; }
    std::optional<LedState> originalState() const { return m_original; }
    std::optional<QList<HueSat>> originalPerKey() const { return m_originalPerKey; }
    GradientAnimator *animator() const { return m_animator; }

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
    std::optional<LightingScene> resolve(const QString &id) const;
    bool applyScene(const LightingScene &scene);
    void captureOriginal();
    void persist(bool dirty);

    KeyboardBackend *m_backend;
    ProfileManager *m_profiles;
    GradientAnimator *m_animator;
    QString m_statePath;
    QString m_defaultProfile = KeyboardProfileId;
    QString m_manual;
    QString m_game;
    QString m_gameLabel;
    std::optional<LightingScene> m_preview;
    std::optional<LedState> m_original;
    std::optional<QList<HueSat>> m_originalPerKey;
    std::optional<LightingScene> m_lastApplied;
    bool m_perKeyDirty = false; // per-key table differs from m_originalPerKey
    QString m_lastActive;
};
