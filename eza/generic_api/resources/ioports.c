#include <eza/arch/types.h>
#include <eza/errno.h>
#include <eza/security.h>
#include <eza/smp.h>
#include <eza/semaphore.h>
#include <eza/swks.h>
#include <eza/resource.h>
#include <mlibc/rbtree.h>
#include <mm/pfalloc.h>
#include <mlibc/string.h>
#include <kernel/syscalls.h>

static DEFINE_MUTEX(ioports_lock);
static struct rb_root ioports_root;

#define LOCK_IOPORTS  semaphore_down(&ioports_lock)
#define UNLOCK_IOPORTS  semaphore_up(&ioports_lock)

typedef struct __ioport_range {
  struct rb_node node;
  ulong_t start_port,end_port;
  task_t *owner;
} ioport_range_t;

static ioport_range_t *__allocate_ioports_range(void)
{
  ioport_range_t *r=alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( r != NULL ) {
    memset(r,0,sizeof(*r));
  }
  return r;
  /* TODO: [mt] Allocate ioport ranges via slabs. */
}

static bool __check_ioports_range(ulong_t first_port,ulong_t num_ports)
{
  struct rb_node *n = ioports_root.rb_node;
  bool avail=true;
  ulong_t last_port=first_port+num_ports-1;

  while(n) {
    ioport_range_t *range=rb_entry(n,ioport_range_t,node);

    if( last_port < range->start_port ) {
      n=n->rb_left;
    } else if( first_port > range->end_port ) {
      n=n->rb_right;
    } else {
      /* Bad luck - this range is busy. */
      avail=false;
      break;
    }
  }

  return avail;
}

static status_t __free_ioports_range(ulong_t start_port,ulong_t num_ports)
{
  struct rb_node *parent=NULL;
  struct rb_node **p=&ioports_root.rb_node;
  ulong_t last_port=start_port+num_ports-1;
  ioport_range_t *r=NULL;

  while( *p ) {
    parent=*p;
    r=rb_entry(parent,ioport_range_t,node);

    /* First, check if we're completely inside of a known range. */
    if( start_port >= r->start_port && last_port <= r->end_port ) {
      if( start_port == r->start_port ) { /* Start cutting from the left edge. */ 
        if( last_port == r->end_port ) {  /* Deleting the whole range. */
          rb_erase(&r->node,&ioports_root);
        } else { /* Just decrease end port. */
          r->start_port=last_port+1;
        }
      } else if( last_port == r->end_port ) {
        r->end_port=start_port-1;
      } else { /* Split this range into two separate ranges. */
        ioport_range_t *nr=__allocate_ioports_range();
        if( nr == NULL ) {
          return -ENOMEM;
        }
        nr->start_port=r->start_port;
        nr->end_port=start_port-1;
        nr->node.rb_right=NULL;
        nr->node.rb_left=r->node.rb_left;
        nr->owner=r->owner;

        r->start_port=last_port+1;

        rb_link_node(&nr->node,&r->node,&r->node.rb_left);
      }

      return 0;
    }

    if( last_port < r->start_port ) {
      p=&(*p)->rb_left;
    } else if( start_port > r->end_port ) {
      p=&(*p)->rb_right;
    } else {
      /* Bad luck - no such range. */
      return -EINVAL;
    }
  }
  return 0;
}

static status_t __add_ioports_range(ulong_t start_port,ulong_t num_ports,
                                    ioport_range_t **out_r,task_t **owner)
{
  struct rb_node *parent=NULL;
  struct rb_node **p=&ioports_root.rb_node;
  ulong_t last_port=start_port+num_ports-1;
  ioport_range_t *r=NULL;

  *out_r=NULL;
  *owner=NULL;

  while( *p ) {
    parent=*p;
    r=rb_entry(parent,ioport_range_t,node);

    /* First, check if we're completely inside of a known range. */
    if( start_port >= r->start_port && last_port <= r->end_port ) {
      *owner=r->owner;
      return -EBUSY;
    }

    if( last_port < r->start_port ) {
      p=&(*p)->rb_left;
    } else if( start_port > r->end_port ) {
      p=&(*p)->rb_right;
    } else {
      /* Bad luck - this range is busy. */
      return -EBUSY;
    }
  }

  /* Check if we can merge new range with existing one. */
  if( r != NULL ) {
    if( (last_port+1) == r->start_port ) {
      r->start_port=start_port;
      *owner=r->owner;
      goto out;
    } else if( (start_port-1) == r->end_port ) {
      r->end_port=last_port;
      *owner=r->owner;
      goto out;
    }
  }

  r=__allocate_ioports_range();
  if( r == NULL ) {
    return -ENOMEM;
  }

  rb_link_node(&r->node,parent,p);
  r->start_port=start_port;
  r->end_port=start_port+num_ports-1;
out:
  *out_r=r;
  return 0;
}

status_t sys_allocate_ioports(ulong_t first_port,ulong_t num_ports)
{
  status_t r;
  task_t *port_owner,*caller;
  ioport_range_t *range;

  if( !security_ops->check_allocate_ioports( current_task(),
                                             first_port,num_ports) ) {
    return -EPERM;
  }

  caller=current_task();
  LOCK_IOPORTS;

  r=__add_ioports_range(first_port,num_ports,&range,&port_owner);
  if( r ) {
    /* This range is already ours ? */
    if( r == -EBUSY && port_owner == caller ) {
      r=0;
    } else {
      goto out_free_ports;
    }
  } else {
    range->owner=caller;
  }

  r=arch_allocate_ioports(caller,first_port,num_ports);
  if( r ) {
    goto out_free_ports;
  }

  UNLOCK_IOPORTS;
  return 0;
out_free_ports:
  __free_ioports_range(first_port,num_ports);  
  UNLOCK_IOPORTS;
  return r;
}

void initialize_ioports(void)
{
}
