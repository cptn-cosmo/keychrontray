#include "lightingcontroller.h"
#include "keyboardbackend.h"
#include "logging.h"
#include "profilemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

LightingController::LightingController(KeyboardBackend *backend, ProfileManager *profiles, const QString &statePath, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_profiles(profiles)
    , m_statePath(statePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/keychrontray/original.json")
                                      : statePath)
{
    connect(m_backend, &KeyboardBackend::connectedChanged, this, &LightingController::onConnectedChanged);
    connect(m_profiles, &ProfileManager::profilesChanged, this, [this] {
        reapply();
    });
}

void LightingController::setDefaultProfile(const QString &id)
{
    m_defaultProfile = id.isEmpty() ? KeyboardProfileId : id;
    reapply();
}

bool LightingController::setManualProfile(const QString &id)
{
    if (id != KeyboardProfileId && !m_profiles->contains(id)) {
        return false;
    }
    m_manual = id;
    reapply();
    return true;
}

void LightingController::restoreDefault()
{
    m_manual.clear();
    reapply();
}

void LightingController::setGameProfile(const QString &id, const QString &gameLabel)
{
    if (id == m_game && gameLabel == m_gameLabel) {
        return;
    }
    m_game = id;
    m_gameLabel = gameLabel;
    m_manual.clear(); // a game starting/stopping takes over from a manual pick
    reapply();
}

void LightingController::preview(const LedState &state)
{
    m_preview = state;
    reapply();
}

void LightingController::endPreview()
{
    if (m_preview) {
        m_preview.reset();
        reapply();
    }
}

QString LightingController::activeProfileId() const
{
    if (!m_manual.isEmpty()) {
        return m_manual;
    }
    if (!m_game.isEmpty() && (m_game == KeyboardProfileId || m_profiles->contains(m_game))) {
        return m_game;
    }
    return m_defaultProfile;
}

QString LightingController::activeReason() const
{
    if (m_preview) {
        return QStringLiteral("preview");
    }
    if (!m_manual.isEmpty()) {
        return QStringLiteral("manual");
    }
    if (!m_game.isEmpty() && (m_game == KeyboardProfileId || m_profiles->contains(m_game))) {
        return QStringLiteral("game: %1").arg(m_gameLabel);
    }
    return QStringLiteral("default");
}

std::optional<LedState> LightingController::resolve(const QString &id) const
{
    if (id == KeyboardProfileId) {
        return m_original;
    }
    if (auto p = m_profiles->profile(id)) {
        return p->ledState();
    }
    return std::nullopt;
}

void LightingController::reapply(bool force)
{
    const QString active = activeProfileId();
    const QString reason = activeReason();
    if (active != m_lastActive || force) {
        m_lastActive = active;
        Q_EMIT activeChanged(active, reason);
    }
    if (!m_backend->isConnected()) {
        return;
    }
    std::optional<LedState> target = m_preview ? m_preview : resolve(active);
    if (!target) {
        // Unknown default profile, or original not captured: fall back to original.
        target = m_original;
    }
    if (!target || (!force && m_lastApplied == target)) {
        return;
    }
    if (m_backend->apply(*target)) {
        m_lastApplied = target;
        persist(m_original && *target != *m_original);
        qCInfo(lcProfiles) << "applied" << (m_preview ? QStringLiteral("preview") : active) << "(" << reason << ")";
    } else {
        m_lastApplied.reset();
        Q_EMIT applyFailed(QStringLiteral("Could not apply lighting to the keyboard"));
    }
}

void LightingController::onConnectedChanged(bool connected)
{
    if (!connected) {
        m_lastApplied.reset();
        return;
    }
    captureOriginal();
    reapply(); // m_lastApplied was reset on disconnect, so this re-pushes the active profile when needed
}

void LightingController::captureOriginal()
{
    if (m_original) {
        return;
    }
    // If a previous run exited without restoring, the saved original is the real baseline.
    QFile f(m_statePath);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.value(QLatin1String("dirty")).toBool()) {
            LedState s;
            s.effect = uint8_t(o.value(QLatin1String("effect")).toInt());
            s.hue = uint8_t(o.value(QLatin1String("hue")).toInt());
            s.saturation = uint8_t(o.value(QLatin1String("saturation")).toInt());
            s.brightness = uint8_t(o.value(QLatin1String("brightness")).toInt());
            s.speed = uint8_t(o.value(QLatin1String("speed")).toInt());
            m_original = s;
            qCInfo(lcProfiles) << "previous run did not restore lighting; using saved original" << s.toString();
            return;
        }
    }
    m_original = m_backend->readState();
    if (m_original) {
        qCInfo(lcProfiles) << "captured original lighting" << m_original->toString();
        m_lastApplied = m_original; // that is what the keyboard shows right now
        persist(false);
    }
}

void LightingController::persist(bool dirty)
{
    if (!m_original) {
        return;
    }
    QDir().mkpath(QFileInfo(m_statePath).path());
    QSaveFile f(m_statePath);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    QJsonObject o{
        {QStringLiteral("dirty"), dirty},
        {QStringLiteral("effect"), m_original->effect},
        {QStringLiteral("hue"), m_original->hue},
        {QStringLiteral("saturation"), m_original->saturation},
        {QStringLiteral("brightness"), m_original->brightness},
        {QStringLiteral("speed"), m_original->speed},
    };
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
}

bool LightingController::saveToKeyboard()
{
    if (!m_backend->isConnected() || !m_backend->saveToEeprom()) {
        return false;
    }
    // The saved state is now the keyboard's own default.
    m_original = m_lastApplied;
    persist(false);
    return true;
}

void LightingController::restoreOriginal()
{
    m_preview.reset();
    if (m_original && m_backend->isConnected() && m_lastApplied != m_original) {
        if (m_backend->apply(*m_original)) {
            qCInfo(lcProfiles) << "restored original lighting";
            m_lastApplied = m_original;
        }
    }
    if (m_lastApplied == m_original) {
        persist(false);
    }
}
