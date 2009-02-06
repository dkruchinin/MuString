/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/tevent.h: prototypes and data structures for task events.
 */

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
void exit_task_events(struct __task_struct *target);

#endif
