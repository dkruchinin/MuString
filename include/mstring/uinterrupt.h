#ifndef __UINTERRUPT_H__
#define __UINTERRUPT_H__

#include <arch/types.h>
#include <mstring/interrupt.h>
#include <ipc/port.h>
#include <mstring/event.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <sync/mutex.h>
#include <mstring/def_actions.h>

#define MAX_IRQS_PER_THREAD  32

typedef void (*irq_listener_t)(irq_t irq,void *private_data);

typedef ulong_t irq_counter_t;
typedef ulong_t irq_event_mask_t;

typedef struct __uintr_descr {
  void *private_data;
  irq_t irq_num;
  irq_listener_t listener;
} uintr_descr_t;

#define __IRQ_ARRAY_ACTIVE_BIT  0

typedef struct __irq_counter_array {
  deffered_irq_action_t de;    /* also contains pointer to the owner. */
  irq_event_mask_t *event_mask;
  irq_counter_t *base_addr;
  void *map_addr;
  list_head_t counter_handlers;
  ulong_t num_counters;
  ulong_t flags;
} irq_counter_array_t;

typedef struct __irq_counter_handler {
  list_node_t lnode;
  irq_event_mask_t mask_to_set;
  irq_counter_t *counter;
  irq_counter_array_t *array;
  ulong_t flags;
  irq_t irq_line;
  struct irq_controller *irqcon;
} irq_counter_handler_t;

#define IRQ_COUNTER_REGISTERED  0x1

typedef struct __uspace_irqs {
  mutex_t mutex;
  spinlock_t lock;
  irq_counter_array_t *array;
} uspace_irqs_t;

typedef struct __userspace_events_data {
  uspace_irqs_t uspace_irqs;
} userspace_events_data_t;

struct __userspace_events_data *allocate_task_uspace_events_data(void);
void free_task_uspace_events_data(userspace_events_data_t * );

#define LOCK_TASK_USPACE_IRQS(t)  ;//mutex_lock(&(t)->uspace_events->uspace_irqs.mutex)
#define UNLOCK_TASK_USPACE_IRQS(t) ;//mutex_unlock(&(t)->uspace_events->uspace_irqs.mutex)

#endif
