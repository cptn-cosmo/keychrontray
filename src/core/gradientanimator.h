#pragma once

#include "gradient.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class KeyboardBackend;

// Renders gradients into the keyboard's per-key color table.
//
// showFrame() writes a complete frame synchronously (~33 ms for 104 LEDs on a K10 HE).
// For scrolling gradients, start() then renders a new frame every `frameIntervalMs` and sends it one
// 9-LED packet per event-loop turn (~3 ms each), skipping packets whose colors did not change, so the
// GUI thread is never blocked for long.
class GradientAnimator : public QObject
{
    Q_OBJECT
public:
    explicit GradientAnimator(KeyboardBackend *backend, QObject *parent = nullptr);

    // Writes the frame for phase 0 (static gradients: the only frame). Returns false on I/O failure.
    bool showFrame(const Gradient &g);
    // Writes an explicit table (e.g. the original per-key colors).
    bool writeTable(const QList<HueSat> &table);
    void start(const Gradient &g);
    void stop();
    bool isRunning() const { return m_frameTimer.isActive(); }

    void setFrameIntervalMs(int ms) { m_frameTimer.setInterval(ms); }
    int framesRendered() const { return m_frames; }

Q_SIGNALS:
    void failed();

private:
    void nextFrame();
    void sendNextChunk();
    bool sendChunk(int start);

    KeyboardBackend *m_backend;
    Gradient m_gradient;
    QElapsedTimer m_clock;
    QTimer m_frameTimer;
    QTimer m_chunkTimer;
    QList<HueSat> m_target; // frame being sent
    QList<HueSat> m_sent; // what the keyboard shows
    QList<int> m_pendingChunks;
    int m_frames = 0;
};
