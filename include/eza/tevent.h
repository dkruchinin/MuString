#ifndef __TEVENT_H__
#define  __TEVENT_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <ds/list.h>

#define TASK_EVENT_TERMINATION  0x1
#define NUM_TASK_EVENTS  1

struct __ipc_gen_port;

typedef struct __task_event_ctl_arg {
  ulong_t ev_mask;
  ulong_t port;
} task_event_ctl_arg;

typedef struct __task_event_descr {
  pid_t pid;
  tid_t tid;
  ulong_t ev_mask;
} task_event_descr_t;

typedef struct __task_event_listener {
  struct __ipc_gen_port *port;
  struct __task_struct *listener;
  list_node_t owner_list;
  list_node_t llist;
  ulong_t events;
} task_event_listener_t;

typedef struct __task_events {
  list_head_t my_events;
  list_head_t listeners;
} task_events_t;

#define ALL_TASK_EVENTS_MASK  ((1<<NUM_TASK_EVENTS)-1)

#define LOCK_TASK_EVENTS_R(t)
#define UNLOCK_TASK_EVENTS_R(t)

#define LOCK_TASK_EVENTS_W(t)
#define UNLOCK_TASK_EVENTS_W(t)

void task_event_notify(ulong_t events);
status_t task_event_attach(struct __task_struct *target,
                           struct __task_struct *listener,
                           task_event_ctl_arg *ctl_arg);
status_t task_event_detach(struct __task_struct *target,
                           struct __task_struct *listener,
                           task_event_ctl_arg *ctl_arg);
void exit_task_events(struct __task_struct *target);

#endif
