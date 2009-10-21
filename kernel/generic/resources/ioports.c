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
 * mstring/resources/ioports.c: Generic I/O ports-related logic.
 *
 */

#include <mstring/types.h>
#include <mstring/errno.h>
#include <mstring/smp.h>
#include <sync/mutex.h>
#include <mstring/swks.h>
#include <mstring/resource.h>
#include <ds/rbtree.h>
#include <mm/page_alloc.h>
#include <mstring/string.h>
#include <kernel/syscalls.h>
#include <security/security.h>
#include <mm/slab.h>

static MUTEX_DEFINE(ioports_lock);
static struct rb_root ioports_root;

#define LOCK_IOPORTS  mutex_lock(&ioports_lock)
#define UNLOCK_IOPORTS  mutex_unlock(&ioports_lock)

typedef struct __ioport_range {
  struct rb_node node;
  ulong_t start_port,end_port;
  pid_t holder;
} ioport_range_t;

static ioport_range_t *__allocate_ioports_range(pid_t holder)
{
  ioport_range_t *r=memalloc(sizeof(*r));

  if( r ) {
    memset(r,0,sizeof(*r));
    r->holder=holder;
  }

  return r;
}

static bool __check_ioports_range(ulong_t first_port,ulong_t last_port,
                                  pid_t *holder)
{
  struct rb_node *n = ioports_root.rb_node;

  *holder=0;
  while(n) {
    ioport_range_t *range=rb_entry(n,ioport_range_t,node);

     /* First, check if we're completely inside of a known range. */
    if( first_port >= range->start_port && last_port <= range->end_port ) {
      *holder=range->holder;
      return false;
    }

    if( last_port < range->start_port ) {
      n=n->rb_left;
    } else if( first_port > range->end_port ) {
      n=n->rb_right;
    } else {
      /* Bad luck - this range is busy. */
      return false;
    }
  }
  
  return true;
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
        ioport_range_t *nr=__allocate_ioports_range(r->holder);
        if( nr == NULL ) {
          return -ENOMEM;
        }
        nr->start_port=r->start_port;
        nr->end_port=start_port-1;
        nr->node.rb_right=NULL;
        nr->node.rb_left=r->node.rb_left;

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
                               ioport_range_t **out_r,pid_t *holder)
{
  struct rb_node *parent=NULL;
  struct rb_node **p=&ioports_root.rb_node;
  ioport_range_t *r=NULL;

  *out_r=NULL;
  *holder=0;

  while( *p ) {
    parent=*p;
    r=rb_entry(parent,ioport_range_t,node);

    /* First, check if we're completely inside of a known range. */
    if( start_port >= r->start_port && last_port <= r->end_port ) {
      *holder=r->holder;
      return ERR(-EBUSY);
    }

    if( last_port < r->start_port ) {
      p=&(*p)->rb_left;
    } else if( start_port > r->end_port ) {
      p=&(*p)->rb_right;
    } else {
      /* Bad luck - this range is busy. */
      return ERR(-EBUSY);
    }
  }

  /* Check if we can merge new range with existing one. */
  if( r != NULL ) {
    if( (last_port+1) == r->start_port ) {
      r->start_port=start_port;
      *holder=r->holder;
      goto out;
    } else if( (start_port-1) == r->end_port ) {
      r->end_port=last_port;
      *holder=r->holder;
      goto out;
    }
  }

  r=__allocate_ioports_range(current_task()->pid);
  if( r == NULL ) {
    return ERR(-ENOMEM);
  }

  rb_link_node(&r->node,parent,p);
  r->start_port=start_port;
  r->end_port=last_port;
  *holder=current_task()->pid;
out:
  *out_r=r;
  return 0;
}

static int __check_ioports(ulong_t first_port,ulong_t end_port)
{
  if( !s_check_system_capability(SYS_CAP_IO_PORT) ) {
    return ERR(-EPERM);
  }

  if( end_port < first_port || first_port >= swks.ioports_available ||
      end_port >= swks.ioports_available ) {
    return ERR(-EINVAL);
  }

  return 0;
}

int sys_allocate_ioports(ulong_t first_port,ulong_t num_ports)
{
  int r;
  task_t *caller=current_task();
  ioport_range_t *range;
  ulong_t end_port=first_port+num_ports-1;
  pid_t port_owner;

  r=__check_ioports(first_port,end_port);
  if( r ) {
    return ERR(r);
  }

  LOCK_IOPORTS;

  r=__add_ioports_range(first_port,end_port,&range,&port_owner);
  if( r ) {
    /* This range is already ours ? */
    if( r == -EBUSY && port_owner == caller->pid ) {
      r=0;
    }
    goto out;
  }

  r=arch_allocate_ioports(caller,first_port,end_port);
  if( r ) {
    __free_ioports_range(first_port,end_port);
  }
out:
  UNLOCK_IOPORTS;
  return ERR(r);
}

int sys_free_ioports(ulong_t first_port,ulong_t num_ports)
{
  int r;
  task_t *caller=current_task();
  ulong_t end_port=first_port+num_ports-1;
  pid_t port_owner;

  r=__check_ioports(first_port,end_port);
  if( r ) {
    return ERR(r);
  }

  LOCK_IOPORTS;
  __check_ioports_range(first_port,end_port,&port_owner);
  if( port_owner != caller->pid ) {
    r=(port_owner !=0) ? -EACCES : -EINVAL;
  } else {
    __free_ioports_range(first_port,end_port);
    r=arch_free_ioports(caller,first_port,end_port);
  }
  UNLOCK_IOPORTS;
  return ERR(r);
}

void initialize_ioports(void)
{
}
