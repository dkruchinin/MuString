#include <mstring/types.h>
#include <ipc/port.h>
#include <mstring/task.h>
#include <mstring/interrupt.h>
#include <mstring/errno.h>
#include <mstring/uinterrupt.h>
#include <sync/spinlock.h>
#include <mm/slab.h>
#include <mstring/smp.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/vmm.h>
#include <mstring/scheduler.h>
#include <mstring/kconsole.h>
#include <mstring/usercopy.h>
#include <mstring/def_actions.h>
#include <security/security.h>

#define IRQCTRL_NAME_MAXLEN 64

static SPINLOCK_DEFINE(descrs_lock, "Uinterrupt descriptors");
static uintr_descr_t descriptors[IRQ_VECTORS];

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
  irq_line_unregister(irq);
  __clean_interrupt_descriptor(irq);
}

static long register_interrupt_listener(irq_t irq,irq_listener_t listener,
                                            void *private_data)
{
  long r;
  uintr_descr_t *descr;
  struct irq_action *iaction = NULL;

  if( irq >= IRQ_VECTORS || !listener )
    return -EINVAL;

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

  if (!irq_line_is_registered(irq)) {
    return ERR(-EINVAL);
  }

  iaction = memalloc(sizeof(*iaction));
  if (!iaction) {
    goto error;
  }

  iaction->name = "User interrupt";
  iaction->handler = __raw_uinterrupt_handler;
  iaction->priv_data = descr;

  r = irq_register_action(irq, iaction);
  if( r ) {
    goto error;
  } else {
    r=0;
  }

  return ERR(r);
error:
  __clean_interrupt_descriptor(irq);
  if (iaction) {
    memfree(iaction);
  }
out_unlock:
  UNLOCK_DESCRIPTORS;
  return ERR(r);
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
    spinlock_initialize(&uirqs->lock, "UIRQ");
    uirqs->array=NULL;
  }

  return uevents;
}

static bool __irq_array_event_checker(void *priv)
{
  event_t *event = (event_t *)priv;
  irq_counter_array_t *array = (irq_counter_array_t *)event->private_data;
  irq_event_mask_t *ev_mask = array->event_mask;
  bool c;

  c = *ev_mask == 0;
  if( c ) {
    array->de.d._event.waitcnt = 1;
  }

  return c;
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
                      array);

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

  if( !s_check_system_capability(SYS_CAP_IRQ) ) {
    return ERR(-EPERM);
  }

  if( !irq_array || !irqs || irqs > MAX_IRQS_PER_THREAD ||
      (addr & PAGE_MASK) ) {
    return ERR(-EINVAL);
  }

  if( !valid_user_address_range(irq_array,irqs*sizeof(ulong_t)) ||
      !valid_user_address_range(addr,PAGE_SIZE) ) {
    return ERR(-EFAULT);
  }

  /* Make user address be visible to kernel IRQ handlers. */
  pframe=NULL;
  caller=current_task();

  LOCK_TASK_VM(caller);
  pfn = vaddr_to_pidx(task_get_rpd(caller), addr);
  if(pfn != PAGE_IDX_INVAL) {
    pframe = pframe_by_id(pfn);
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
#if 0
    if( !valid_irq_number(ids[i]) ) {
      id=-EINVAL;
      goto out;
    }
#endif
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
    h->irqcon = irq_get_line_controller(ids[i]);

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
  return ERR(id);
}

static irq_counter_array_t *__get_irq_array(task_t *task,ulong_t id)
{
  /* TODO: [mt] Support more than one IRQ interrupts array per task ! */
  if( !id ) {
    return task->uspace_events->uspace_irqs.array;
  }
  return NULL;
}

int sys_wait_on_irq_array(ulong_t id)
{
  irq_counter_array_t *array=__get_irq_array(current_task(),id);

  if( !array ) {
    return ERR(-EINVAL);
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

  return 0;
}

long sys_register_free_irq(const char *irq_backend)
{
  char *name[IRQCTRL_NAME_MAXLEN];
  long ret;
  irq_t irq;
  struct irq_controller *irqctrl;

  if (copy_from_user(name, (char*)irq_backend, IRQCTRL_NAME_MAXLEN))
    return ERR(-EFAULT);

  name[IRQCTRL_NAME_MAXLEN - 1] = '\0';
  irqctrl = irq_get_controller((char*)name);
  if (irqctrl == NULL) {
    ret = -EINVAL;
  } else {
    ret = irq_register_free_line(irqctrl, &irq);
    if (!ret)
      ret = (long)irq;
  }

  return ERR(ret);
}

int sys_unregister_irq(irq_t irq)
{
  return irq_line_unregister(irq);
}
