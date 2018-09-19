#ifndef __EVENT_MONITOR_H__
#define __EVENT_MONITOR_H__

#include "../core/types.h"

// event monitor color
#define EVENTMONITOR_COLOR_BUTTONACTIVE RGB(79, 204, 11) // bright green

// user event: set bit 31 to 1 in order not to duplicate with the already defined events.
#define EVENT_USER_TESTMESSAGE 0x80000001

void k_eventMonitorTask(void);

#endif // __EVENT_MONITOR_H__