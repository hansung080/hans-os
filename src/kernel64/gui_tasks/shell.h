#ifndef __GUITASKS_SHELL_H__
#define __GUITASKS_SHELL_H__

#include "../core/types.h"

// title
#define GUISH_TITLE "Shell"

// color
#define GUISH_COLOR_TEXT       RGB(33, 147, 176)
#define GUISH_COLOR_BACKGROUND RGB(0, 0, 0)

void k_guiShellTask(void);
static void k_processConsoleScreenBuffer(qword windowId);

extern volatile qword g_guiShellId;

#endif // __GUITASKS_SHELL_H__