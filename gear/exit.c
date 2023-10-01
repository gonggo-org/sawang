#include <signal.h>
#include <unistd.h>

#include "exit.h"

void sawang_kill() {
    dispatcher_deactivate_on_exit = false;
    sawang_exit = true;
    kill(getpid(), SIGTERM);
}
