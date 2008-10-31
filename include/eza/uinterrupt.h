#ifndef __UINTERRUPT_H__
#define __UINTERRUPT_H__

#include <eza/arch/types.h>
#include <eza/interrupt.h>
#include <ipc/port.h>
#include <eza/event.h>
#include <ds/list.h>
#include <eza/semaphore.h>
#include <eza/spinlock.h>

#define MAX_IRQS_PER_THREAD  32

typedef void (*irq_listener_t)(irq_t irq,void *private_data);

typedef ulong_t irq_counter_t;
typedef ulong_t irq_event_mask_t;

typedef struct __uintr_descr {
  void *private_data;
  irq_t irq_num;
  irq_listener_t listener;
} uintr_descr_t;

typedef struct __irq_counter_array {
  event_t event;    /* also contains pointer to the owner. */
  irq_event_mask_t *event_mask;
  irq_counter_t *base_addr;
  void *map_addr;
  list_head_t counter_handlers;
  ulong_t num_counters;  
} irq_counter_array_t;

typedef struct __irq_counter_handler {
  list_node_t lnode;
  irq_event_mask_t mask_to_set;
  irq_counter_t *counter;
  irq_counter_array_t *array;
  ulong_t flags;
  irq_t irq_line;  
} irq_counter_handler_t;

#define IRQ_COUNTER_REGISTERED  0x1

typedef struct __uspace_irqs {
    //mutex_t mutex; /* FIXME DK: uncomment this field after mutex API becomes ready */
  spinlock_t lock;
  irq_counter_array_t *array;
} uspace_irqs_t;

typedef struct __userspace_events_data {
  uspace_irqs_t uspace_irqs;
} userspace_events_data_t;

struct __userspace_events_data *allocate_task_uspace_events_data(void);

#define LOCK_TASK_USPACE_IRQS(t)
#define UNLOCK_TASK_USPACE_IRQS(t)

#endif
