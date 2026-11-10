#include "lightingcontroller.h"
#include "gradientanimator.h"
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

LightingScene LightingScene::fromProfile(const Profile &p)
{
    return LightingScene{p.ledState(), p.gradient};
}

LightingController::LightingController(KeyboardBackend *backend, ProfileManager *profiles, const QString &statePath, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_profiles(profiles)
    , m_animator(new GradientAnimator(backend, this))
    , m_statePath(statePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/keychrontray/original.json")
                                      : statePath)
{
    connect(m_backend, &KeyboardBackend::connectedChanged, this, &LightingController::onConnectedChanged);
    connect(m_profiles, &ProfileManager::profilesChanged, this, [this] {
        reapply();
    });
    connect(m_animator, &GradientAnimator::failed, this, [this] {
        m_lastApplied.reset();
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

void LightingController::preview(const LightingScene &scene)
{
    m_preview = scene;
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

std::optional<LightingScene> LightingController::resolve(const QString &id) const
{
    if (id == KeyboardProfileId) {
        if (!m_original) {
            return std::nullopt;
        }
        return LightingScene{*m_original, std::nullopt};
    }
    if (auto p = m_profiles->profile(id)) {
        return LightingScene::fromProfile(*p);
    }
    return std::nullopt;
}

bool LightingController::applyScene(const LightingScene &scene)
{
    m_animator->stop();
    LedState base = scene.base;
    if (scene.gradient) {
        if (m_backend->perKeyLedCount() > 0) {
            // Per-key table first, then switch the effect: no flash of stale colors.
            if (!m_animator->showFrame(*scene.gradient)) {
                return false;
            }
            m_perKeyDirty = m_originalPerKey.has_value(); // only meaningful if there is a table to restore
            base.effect = Effects::PerKeyRgb;
        } else if (base.effect == Effects::PerKeyRgb) {
            base.effect = 1; // no per-key support: solid first color
        }
    } else if (m_perKeyDirty && m_originalPerKey && base.effect == Effects::PerKeyRgb) {
        // A per-key scene (e.g. the original) after our gradients: put the original table back first.
        if (!m_animator->writeTable(*m_originalPerKey)) {
            return false;
        }
        m_perKeyDirty = false;
    }
    if (!m_backend->apply(base)) {
        return false;
    }
    if (scene.gradient && m_backend->perKeyLedCount() > 0) {
        m_animator->start(*scene.gradient);
    }
    return true;
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
    std::optional<LightingScene> target = m_preview ? m_preview : resolve(active);
    if (!target && m_original) {
        // Unknown default profile, or original not captured: fall back to original.
        target = LightingScene{*m_original, std::nullopt};
    }
    if (!target || (!force && m_lastApplied == target)) {
        return;
    }
    if (applyScene(*target)) {
        m_lastApplied = target;
        persist(m_perKeyDirty || (m_original && target->base != *m_original));
        qCInfo(lcProfiles) << "applied" << (m_preview ? QStringLiteral("preview") : active) << "(" << reason << ")"
                           << (target->gradient ? (target->gradient->motion == Gradient::Motion::Scroll ? "scrolling gradient" : "static gradient") : "");
    } else {
        m_lastApplied.reset();
        Q_EMIT applyFailed(QStringLiteral("Could not apply lighting to the keyboard"));
    }
}

void LightingController::onConnectedChanged(bool connected)
{
    if (!connected) {
        m_animator->stop();
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
            const QByteArray pk = QByteArray::fromHex(o.value(QLatin1String("perKey")).toString().toLatin1());
            if (!pk.isEmpty() && pk.size() % 2 == 0) {
                QList<HueSat> t;
                for (int i = 0; i < pk.size(); i += 2) {
                    t.append(HueSat{uint8_t(pk[i]), uint8_t(pk[i + 1])});
                }
                m_originalPerKey = t;
                m_perKeyDirty = true;
            }
            if (!m_originalPerKey) {
                m_perKeyDirty = false; // older state file: the original per-key table is unknown, nothing to restore
            }
            qCInfo(lcProfiles) << "previous run did not restore lighting; using saved original" << s.toString();
            return;
        }
    }
    m_original = m_backend->readState();
    if (m_original) {
        m_originalPerKey = m_backend->readPerKey();
        qCInfo(lcProfiles) << "captured original lighting" << m_original->toString() << (m_originalPerKey ? "+ per-key table" : "");
        m_lastApplied = LightingScene{*m_original, std::nullopt}; // that is what the keyboard shows right now
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
    if (m_originalPerKey) {
        QByteArray pk;
        for (const HueSat &hs : std::as_const(*m_originalPerKey)) {
            pk.append(char(hs.h));
            pk.append(char(hs.s));
        }
        o.insert(QStringLiteral("perKey"), QString::fromLatin1(pk.toHex()));
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
}

bool LightingController::saveToKeyboard()
{
    if (!m_backend->isConnected()) {
        return false;
    }
    m_animator->stop(); // a scrolling gradient is saved as its current frame
    const bool ok = m_backend->saveToEeprom();
    if (ok) {
        // The saved state is now the keyboard's own default.
        m_original = m_lastApplied ? m_lastApplied->base : m_backend->readState();
        if (m_original && m_lastApplied && m_lastApplied->gradient && m_backend->perKeyLedCount() > 0) {
            m_original->effect = Effects::PerKeyRgb;
        }
        m_originalPerKey = m_backend->readPerKey();
        m_perKeyDirty = false;
        persist(false);
    }
    if (m_lastApplied && m_lastApplied->gradient) {
        m_animator->start(*m_lastApplied->gradient);
    }
    return ok;
}

void LightingController::restoreOriginal()
{
    m_animator->stop();
    m_preview.reset();
    if (!m_original || !m_backend->isConnected()) {
        return;
    }
    if (m_perKeyDirty && m_originalPerKey && m_animator->writeTable(*m_originalPerKey)) {
        m_perKeyDirty = false;
    }
    const LightingScene orig{*m_original, std::nullopt};
    if (m_lastApplied != orig && m_backend->apply(*m_original)) {
        qCInfo(lcProfiles) << "restored original lighting";
        m_lastApplied = orig;
    }
    if (m_lastApplied == orig && !m_perKeyDirty) {
        persist(false);
    }
}
