#pragma once

#include <QList>
#include <QSet>

// Decides when to fire low-battery notifications: once per threshold crossing (downwards).
// A threshold re-arms when the level rises above it again (by `hysteresis`) or the keyboard is charging.
class BatteryAlerts
{
public:
    explicit BatteryAlerts(QList<int> thresholds = {20, 10}, int hysteresis = 3);

    void setThresholds(QList<int> thresholds);
    QList<int> thresholds() const { return m_thresholds; }

    // Feed a new reading; returns the threshold to notify about (the lowest crossed), or -1.
    int update(int percent, bool charging);

private:
    QList<int> m_thresholds; // sorted descending
    QSet<int> m_fired;
    int m_hysteresis;
};
