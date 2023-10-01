#ifndef _SAWANG_DISPATCHER_H_
#define _SAWANG_DISPATCHER_H_

#include <stdbool.h>

#include "log.h"
#include "define.h"

enum ActivationState {
  ACTIVATION_IDLE = 0,

  ACTIVATION_REQUEST = 1,
  ACTIVATION_FAILED = 2,
  ACTIVATION_SUCCESS = 3,
  ACTIVATION_DONE = 4,

  DEACTIVATION_REQUEST = 11,
  DEACTIVATION_NOTEXISTS = 12,
  DEACTIVATION_SUCCESS = 13,
  DEACTIVATION_DONE = 14
};

typedef struct ActivationData {
    pthread_mutex_t mtx;
    pthread_cond_t cond_idle;
    pthread_cond_t cond_dispatcher_wakeup;
    pthread_cond_t cond_proxy_wakeup;
    char sawang_name[SHMPATHBUFLEN];
    long proxy_heartbeat;
    long dispatcher_heartbeat;
    enum ActivationState state;
} ActivationData;

extern bool dispatcher_activate(const char *gonggo_name, const char *sawang_name,
    long heartbeat, long *dispatcher_heartbeat, const LogContext * log_ctx);
extern void dispatcher_deactivate(const char *gonggo_name, const char *sawang_name, const LogContext * log_ctx);

#endif //_SAWANG_DISPATCHER_H_
