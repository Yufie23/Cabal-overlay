// ─────────────────────────────────────────────────────────────
// alarms.h — warn-before-event notifications
//
// AlarmTracker watches the configured schedule and reports each
// event once when its next occurrence falls inside the warning
// window. Pure logic + a memory of what it already fired — it does
// NOT send notifications itself; the caller passes a callback and
// owns the delivery mechanism (GNotification in main.cpp today,
// sound later). Portable: no GTK anywhere in sight.
// ─────────────────────────────────────────────────────────────
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/config.h"

class AlarmTracker {
public:
    // Calls on_alarm(event, minutes_until_start) at most once per
    // occurrence of every alarm-enabled event whose next start is
    // within `config.warn_before_min` minutes. Firing again for the
    // same occurrence (e.g. after the tick keeps running while the
    // event approaches) is suppressed; the NEXT occurrence gets its
    // own alarm.
    void check(const std::vector<ScheduleEvent>& events,
               const AlarmsConfig& config,
               std::chrono::system_clock::time_point now,
               const std::function<void(const ScheduleEvent&, int minutes)>&
                   on_alarm);

private:
    // Event id → the occurrence we already alarmed about. An entry
    // that never updates again is harmless: one small pair per event
    // for the app's whole lifetime.
    std::map<std::string, std::chrono::system_clock::time_point> m_fired;
};
