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
 * eza/resources/ioports.c: Generic I/O ports-related logic.
 *
 */

#include <mlibc/types.h>
#include <eza/errno.h>
#include <eza/security.h>
#include <eza/smp.h>
#include <eza/mutex.h>
#include <eza/swks.h>
#include <eza/resource.h>
#include <ds/rbtree.h>
#include <mm/pfalloc.h>
#include <mlibc/string.h>
#include <kernel/syscalls.h>

static MUTEX_DEFINE(ioports_lock);
static struct rb_root ioports_root;

#define LOCK_IOPORTS  mutex_lock(&ioports_lock)
#define UNLOCK_IOPORTS  mutex_unlock(&ioports_lock)

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

static bool __check_ioports_range(ulong_t first_port,ulong_t last_port,
                                  task_t **owner)
{
  struct rb_node *n = ioports_root.rb_node;
  bool avail=true;

  *owner=NULL;
  while(n) {
    ioport_range_t *range=rb_entry(n,ioport_range_t,node);

    /* First, check if we're completely inside of a known range. */
    if( first_port >= range->start_port && last_port <= range->end_port ) {
      *owner=range->owner;
      return false;
    }

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

static int __free_ioports_range(ulong_t start_port,ulong_t last_port)
{
  struct rb_node *parent=NULL;
  struct rb_node **p=&ioports_root.rb_node;
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

static int __add_ioports_range(ulong_t start_port,ulong_t last_port,
                                    ioport_range_t **out_r,task_t **owner)
{
  struct rb_node *parent=NULL;
  struct rb_node **p=&ioports_root.rb_node;
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
  r->end_port=last_port;
out:
  *out_r=r;
  return 0;
}

static int __check_ioports( task_t *task,ulong_t first_port,
                                 ulong_t end_port)
{
  if( !security_ops->check_access_ioports(current_task(),
                                          first_port,end_port) ) {
    return -EPERM;
  }

  kprintf("%p < %p || %p >= %p || %p >= %p\n",
          end_port, first_port, first_port, swks.ioports_available,
          end_port, swks.ioports_available);
  if( end_port < first_port || first_port >= swks.ioports_available ||
      end_port >= swks.ioports_available ) {
    return -EINVAL;
  }

  return 0;
}

int sys_allocate_ioports(ulong_t first_port,ulong_t num_ports)
{
  int r;
  task_t *port_owner,*caller;
  ioport_range_t *range;
  ulong_t end_port=first_port+num_ports-1;

  kprintf("I'm going to allocate  %p to %p\n",  first_port, first_port + num_ports);
  caller=current_task();
  r=__check_ioports(caller,first_port,end_port);
  if( r ) {
    kprintf("SUJA!!!! %d\n", r);
    return r;
  }
  
  LOCK_IOPORTS;

  r=__add_ioports_range(first_port,end_port,&range,&port_owner);
  if( r ) {
    /* This range is already ours ? */
    if( r == -EBUSY && port_owner == caller ) {
      r=0;
    } else {
      goto out;
    }
  } else {
    range->owner=caller;
  }

  UNLOCK_IOPORTS;
  
  r=arch_allocate_ioports(caller,first_port,end_port);
  if( r ) {
    LOCK_IOPORTS;
    goto out_free_ports;
  }

  kprintf("OK Port %p to %p: %d\n", first_port, first_port + num_ports, r);
  return 0;
out_free_ports:
  __free_ioports_range(first_port,end_port);
out:
  UNLOCK_IOPORTS;
  kprintf("Port %p to %p: %d\n", first_port, first_port + num_ports, r);
  return r;
}

int sys_free_ioports(ulong_t first_port,ulong_t num_ports)
{
  int r;
  task_t *port_owner,*caller;
  ulong_t end_port;

  end_port=first_port+num_ports-1;

  caller=current_task();
  r=__check_ioports(caller,first_port,num_ports);
  if( r ) {
    return r;
  }

  LOCK_IOPORTS;
  __check_ioports_range(first_port,end_port,&port_owner);
  if( port_owner != caller ) {
    r=-EACCES;
    goto out; 
  }

  __free_ioports_range(first_port,end_port);
  r=arch_free_ioports(caller,first_port,end_port);
out:
  UNLOCK_IOPORTS;
  return r;
}

void initialize_ioports(void)
{
}
