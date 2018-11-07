#ifndef __CORE_WINDOWMANAGER_H__
#define __CORE_WINDOWMANAGER_H__

#include "types.h"

// data integration count: It's recommended to be the same number as WINDOW_MAXCOPIEDAREAARRAYCOUNT.
#define WINDOWMANAGER_DATAINTEGRATIONCOUNT 20

void k_windowManagerTask(void);
static void k_mouseDataTask(void);
static void k_keyTask(void);
static bool k_processMouseData(void);
static bool k_processKey(void);
static bool k_processWindowManagerEvent(void);

#if __DEBUG__
extern volatile qword g_winMgrMinLoopCount;
#endif // __DEBUG__

#endif // __CORE_WINDOWMANAGER_H__