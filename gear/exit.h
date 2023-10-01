#ifndef _SAWANG_EXIT_H_
#define _SAWANG_EXIT_H_

#include <stdbool.h>

extern volatile bool sawang_exit;
extern volatile bool dispatcher_deactivate_on_exit;

extern void sawang_kill();

#endif //_SAWANG_EXIT_H_
