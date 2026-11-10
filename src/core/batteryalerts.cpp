#include "batteryalerts.h"

#include <algorithm>

BatteryAlerts::BatteryAlerts(QList<int> thresholds, int hysteresis)
    : m_hysteresis(hysteresis)
{
    setThresholds(std::move(thresholds));
}

void BatteryAlerts::setThresholds(QList<int> thresholds)
{
    thresholds.removeIf([](int t) {
        return t <= 0 || t >= 100;
    });
    std::sort(thresholds.begin(), thresholds.end(), std::greater<int>());
    thresholds.erase(std::unique(thresholds.begin(), thresholds.end()), thresholds.end());
    m_thresholds = thresholds;
    m_fired.intersect(QSet<int>(thresholds.begin(), thresholds.end()));
}

int BatteryAlerts::update(int percent, bool charging)
{
    if (percent < 0) {
        return -1;
    }
    int fire = -1;
    for (int t : std::as_const(m_thresholds)) {
        if (charging || percent >= t + m_hysteresis) {
            m_fired.remove(t);
        } else if (percent <= t && !m_fired.contains(t)) {
            m_fired.insert(t);
            fire = t; // keep the lowest (list is descending)
        }
    }
    return fire;
}
