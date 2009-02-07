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
 * include/eza/gc.h: prototypes and data structures for GC thread actions.
 */

#ifndef __GC_H__
#define  __GC_H__

#include <config.h>
#include <eza/arch/types.h>
#include <ds/list.h>
#include <eza/task.h>
#include <eza/smp.h>

#define NUM_MASTER_PERCPU_THREADS 1

#ifdef CONFIG_SMP
  #define NUM_SMP_PERCPU_THREADS  1
  #define NUM_PERCPU_THREADS  (NUM_MASTER_PERCPU_THREADS+ \
                               NUM_SMP_PERCPU_THREADS)
#else
  #define NUM_PERCPU_THREADS  NUM_MASTER_PERCPU_THREADS
#endif


task_t *gc_threads[CONFIG_NRCPUS][NUM_PERCPU_THREADS];

struct __gc_action;

typedef void (*gc_action_dtor_t)(struct __gc_action *action);
typedef void (*gc_actor_t)(void *data,ulong_t data_arg);
typedef void (*actor_t)(void *data);

typedef struct __gc_action {
  ulong_t type;
  gc_action_dtor_t dtor;
  gc_actor_t action;
  void *data;
  ulong_t data_arg;
  list_head_t data_list_head;
  list_node_t l;
} gc_action_t;

void initialize_gc(void);
gc_action_t *gc_allocate_action(gc_actor_t actor, void *data,
                                ulong_t data_arg);
void gc_schedule_action(gc_action_t *action);
void gc_free_action(gc_action_t *action);
void spawn_percpu_threads(void);

#define GC_TASK_RESOURCE  0x1

static inline void gc_init_action(gc_action_t *action,gc_actor_t actor,
                                  void *data,long_t data_arg)
{
  action->action=actor;
  action->data=data;
  list_init_node(&action->l);
  list_init_head(&action->data_list_head);
  action->type=0;
  action->data_arg=data_arg;
  action->dtor=NULL;
}

static inline void gc_put_action(gc_action_t *action)
{
  if(action->dtor) {
    action->dtor(action);
  }
}

#define GC_THREAD_IDX  1 /**< Index of the GC thread in the array of per-CPU threads. **/
#define MIGRATION_THREAD_IDX  0  /**< Index of the migration thread in the array of per-CPU threads. **/

#ifdef CONFIG_SMP

typedef struct __migration_action_t {
  task_t *task;
  event_t e;
  list_node_t l;
  status_t status;
  cpu_id_t target_cpu;
} migration_action_t;

status_t schedule_task_migration(migration_action_t *a,cpu_id_t cpu);

#endif

#endif
