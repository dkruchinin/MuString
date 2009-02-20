#include <mlibc/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/interrupt.h>
#include <eza/errno.h>
#include <eza/uinterrupt.h>
#include <eza/spinlock.h>
#include <mm/slab.h>
#include <eza/smp.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/scheduler.h>
#include <eza/kconsole.h>
#include <eza/usercopy.h>
#include <eza/def_actions.h>

static SPINLOCK_DEFINE(descrs_lock);
static uintr_descr_t descriptors[NUM_IRQS];

#define LOCK_DESCRIPTORS spinlock_lock(&descrs_lock)
#define UNLOCK_DESCRIPTORS spinlock_unlock(&descrs_lock)
#define LOCK_TASK_VM(t)
#define UNLOCK_TASK_VM(t)

static void __raw_uinterrupt_handler(void *priv)
{
  uintr_descr_t *descr=(uintr_descr_t *)priv;

  descr->listener(descr->irq_num,descr->private_data);
}

static void __clean_interrupt_descriptor(irq_t irq)
{
  uintr_descr_t *descr=&descriptors[irq];

  LOCK_DESCRIPTORS;
  descr->listener=NULL;
  descr->private_data=NULL;
  descr->irq_num=0;
  UNLOCK_DESCRIPTORS;
}

static void __unregister_interrupt_listener(irq_t irq,void *private_data)
{
  unregister_irq(irq,private_data);
  __clean_interrupt_descriptor(irq);
}

static long register_interrupt_listener(irq_t irq,irq_listener_t listener,
                                            void *private_data)
{
  long r;
  uintr_descr_t *descr;

  if( irq >= NUM_IRQS || !listener ) {
    return -EINVAL;
  }

  descr=&descriptors[irq];

  LOCK_DESCRIPTORS;
  if( descr->listener ) {
    r=-EBUSY;
    goto out_unlock;
  } else {
    descr->listener = listener;
  }
  UNLOCK_DESCRIPTORS;

  descr->private_data=private_data;
  descr->irq_num=irq;

  r=register_irq(irq,__raw_uinterrupt_handler,descr,0);
  if( r ) {
    __clean_interrupt_descriptor(irq);
  } else {
    r=0;
  }

  return r;
out_unlock:
  UNLOCK_DESCRIPTORS;
  return r;
}

static void __free_irq_counter_array(irq_counter_array_t *array)
{
  irq_counter_handler_t *handler;
  list_node_t *n;

  list_for_each(&array->counter_handlers,n) {
    handler=container_of(n,irq_counter_handler_t,lnode);

    if( handler->flags & IRQ_COUNTER_REGISTERED ) {
      __unregister_interrupt_listener(handler->irq_line,handler);
    }
    memfree(handler);
  }
}

struct __userspace_events_data *allocate_task_uspace_events_data(void)
{
  userspace_events_data_t *uevents=memalloc(sizeof(userspace_events_data_t));

  if( uevents ) {
    uspace_irqs_t *uirqs=&uevents->uspace_irqs;

    /* Initialize IRQs-related stuff. */
    //mutex_initialize(&uirqs->mutex); /* FIXME DK: uncomment later... (u know when :)) */
    spinlock_initialize(&uirqs->lock);
    uirqs->array=NULL;
  }

  return uevents;
}

static bool __irq_array_event_checker(void *priv)
{
  irq_event_mask_t *ev_mask=(irq_event_mask_t*)priv;
  return *ev_mask == 0;
}

static irq_counter_array_t *__allocate_irq_counter_array(task_t *task,ulong_t nc)
{
  irq_counter_array_t *array=memalloc(sizeof(irq_counter_array_t));

  if( array ) {
    array->num_counters=nc;
    DEFFERED_ACTION_INIT(&array->de,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);

    list_init_head(&array->counter_handlers);
    event_initialize(&array->de.d._event);
    event_set_checker(&array->de.d._event,__irq_array_event_checker,
                      &array->event_mask);

    array->map_addr=NULL;
    array->event_mask=0;
    array->flags=0;
  }
  return array;
}

static void __raw_irq_array_handler(irq_t irq,void *priv)
{
  irq_counter_handler_t *h=(irq_counter_handler_t *)priv;

  *h->array->event_mask |= h->mask_to_set;
  (*h->counter)++;
  if( arch_bit_test(&h->array->flags,__IRQ_ARRAY_ACTIVE_BIT) ) {
    schedule_deffered_action(&h->array->de);
  }
}

long sys_create_irq_counter_array(ulong_t irq_array,ulong_t irqs,
                                      ulong_t addr,ulong_t flags)
{
  long id;
  irq_counter_array_t *array;
  page_idx_t pfn;
  ulong_t *ids;
   task_t *caller;
  ulong_t i;
  irq_counter_handler_t *h;
  page_frame_t *pframe;
  ulong_t *kaddr;

  if( !irq_array || !irqs || irqs > MAX_IRQS_PER_THREAD ||
      (addr & PAGE_MASK) ) {
    return -EINVAL;
  }

  if( !valid_user_address_range(irq_array,irqs*sizeof(ulong_t)) ||
      !valid_user_address_range(addr,PAGE_SIZE) ) {
    return -EFAULT;
  }

  /* Make user address be visible to kernel IRQ handlers. */
  pframe=NULL;
  caller=current_task();

  LOCK_TASK_VM(caller);
  pfn = ptable_ops.vaddr2page_idx(task_get_rpd(caller), addr);
  if(pfn != PAGE_IDX_INVAL) {
    pframe = pframe_by_number(pfn);
    pin_page_frame(pframe);
    kaddr=pframe_to_virt(pframe);
  }
  UNLOCK_TASK_VM(caller);

  if( !pframe ) {
    id=-EFAULT;
    goto unlock;
  }

  LOCK_TASK_USPACE_IRQS(caller);
  if( caller->uspace_events->uspace_irqs.array ) {
    id=-EBUSY;
    goto unlock;
  }

  /* Allocate temporary array for storing user IRQ numbers. */
  ids=(ulong_t *)memalloc(sizeof(ulong_t)*irqs);
  if( !ids ) {
    id=-ENOMEM;
    goto unlock;
  }

  if( copy_from_user(ids,(void *)irq_array,sizeof(ulong_t)*irqs) ) {
    id=-EFAULT;
    goto out;
  }

  /* Sanity check: make sure user didn't pass any invalid IRQ numbers. */
  for(i=0;i<irqs;i++) {
    if( !valid_irq_number(ids[i]) ) {
      id=-EINVAL;
      goto out;
    }
  }

  id=-ENOMEM;
  array=__allocate_irq_counter_array(caller,irqs);
  if( !array ) {
    goto out;
  }

  array->event_mask=kaddr;
  *array->event_mask=0;
  array->base_addr=(irq_counter_t*)((char *)kaddr+(sizeof(irq_event_mask_t)));

  for(i=0;i<irqs;i++) {
    h=(irq_counter_handler_t*)memalloc(sizeof(irq_counter_handler_t));
    if( !h ) {
      goto free_array;
    }

    list_init_node(&h->lnode);
    h->counter=&array->base_addr[i];
    h->flags=0;
    h->irq_line=ids[i];
    h->array=array;
    h->mask_to_set=(1<<i);
    *h->counter=0;
 
    list_add2tail(&array->counter_handlers,&h->lnode);
    id=register_interrupt_listener(ids[i],__raw_irq_array_handler,h );
    if( id ) {
      goto free_array;
    }
    h->flags |= IRQ_COUNTER_REGISTERED;
  }

  caller->uspace_events->uspace_irqs.array=array;
  /* TODO: [mt] Assign IDs for IRQ arrays properly [R] ... */
  id=0;
  UNLOCK_TASK_USPACE_IRQS(caller);

  memfree(ids);
  return id;
free_array:
  __free_irq_counter_array(array);
  memfree(array);
out:
  memfree(ids);
unlock:
  UNLOCK_TASK_USPACE_IRQS(caller);
  if(pframe) {
    unpin_page_frame(pframe);
  }
  return id;
}

static irq_counter_array_t *__get_irq_array(task_t *task,ulong_t id)
{
  /* TODO: [mt] Support more than one IRQ interrupts array per task ! */
  if( !id ) {
    return task->uspace_events->uspace_irqs.array;
  }
  return NULL;
}

int ___big_verbose=0;
int ___target_pid=0x111111111;

int sys_wait_on_irq_array(ulong_t id)
{
  irq_counter_array_t *array=__get_irq_array(current_task(),id);

  if( !array ) {
    return -EINVAL;
  }

  interrupts_disable();
  event_reset(&array->de.d._event);
  event_set_task(&array->de.d._event,current_task());
  array->de.priority=current_task()->priority;
  arch_bit_set(&array->flags,__IRQ_ARRAY_ACTIVE_BIT);
  interrupts_enable();

  /* Check the event mask first time. */
  if( !*array->event_mask ) {
    event_yield(&array->de.d._event);
  }

  arch_bit_clear(&array->flags,__IRQ_ARRAY_ACTIVE_BIT);
  event_reset(&array->de.d._event);

  ___target_pid=current_task()->pid;

  return 0;
}
