#include "gradientanimator.h"
#include "keyboardbackend.h"
#include "logging.h"

GradientAnimator::GradientAnimator(KeyboardBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    m_frameTimer.setInterval(100);
    connect(&m_frameTimer, &QTimer::timeout, this, &GradientAnimator::nextFrame);
    m_chunkTimer.setSingleShot(true);
    m_chunkTimer.setInterval(0);
    connect(&m_chunkTimer, &QTimer::timeout, this, &GradientAnimator::sendNextChunk);
}

bool GradientAnimator::sendChunk(int start)
{
    const int n = std::min<int>(KeyboardBackend::MaxPerKeyChunk, m_target.size() - start);
    const QList<HueSat> chunk = m_target.mid(start, n);
    if (!m_backend->writePerKey(start, chunk)) {
        return false;
    }
    if (m_sent.size() != m_target.size()) {
        m_sent = QList<HueSat>(m_target.size());
    }
    for (int i = 0; i < n; ++i) {
        m_sent[start + i] = chunk[i];
    }
    return true;
}

bool GradientAnimator::writeTable(const QList<HueSat> &table)
{
    stop();
    m_target = table;
    m_sent.clear(); // unknown: write everything
    for (int start = 0; start < m_target.size(); start += KeyboardBackend::MaxPerKeyChunk) {
        if (!sendChunk(start)) {
            qCWarning(lcHid) << "per-key write failed";
            return false;
        }
    }
    return true;
}

bool GradientAnimator::showFrame(const Gradient &g)
{
    return writeTable(g.render(m_backend->ledPositions(), 0.0));
}

void GradientAnimator::start(const Gradient &g)
{
    m_gradient = g;
    m_frames = 0;
    m_clock.start();
    if (g.motion == Gradient::Motion::Scroll) {
        m_frameTimer.start();
    }
}

void GradientAnimator::stop()
{
    m_frameTimer.stop();
    m_chunkTimer.stop();
    m_pendingChunks.clear();
}

void GradientAnimator::nextFrame()
{
    if (!m_pendingChunks.isEmpty()) {
        return; // previous frame still being sent: drop this tick
    }
    m_target = m_gradient.render(m_backend->ledPositions(), m_gradient.phaseAt(m_clock.elapsed()));
    ++m_frames;
    for (int start = 0; start < m_target.size(); start += KeyboardBackend::MaxPerKeyChunk) {
        const int n = std::min<int>(KeyboardBackend::MaxPerKeyChunk, m_target.size() - start);
        if (m_sent.size() != m_target.size() || m_sent.mid(start, n) != m_target.mid(start, n)) {
            m_pendingChunks.append(start);
        }
    }
    if (!m_pendingChunks.isEmpty()) {
        m_chunkTimer.start();
    }
}

void GradientAnimator::sendNextChunk()
{
    if (m_pendingChunks.isEmpty()) {
        return;
    }
    if (!sendChunk(m_pendingChunks.takeFirst())) {
        qCWarning(lcHid) << "gradient animation stopped: per-key write failed";
        stop();
        Q_EMIT failed();
        return;
    }
    if (!m_pendingChunks.isEmpty()) {
        m_chunkTimer.start();
    }
}
