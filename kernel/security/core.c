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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * kernel/security/core.c: Common functions of the kernel security facility.
 */

#include <mstring/types.h>
#include <security/security.h>
#include <mstring/task.h>
#include <arch/current.h>
#include <security/util.h>
#include <mm/slab.h>
#include <arch/atomic.h>

static struct __s_object system_caps[SYS_CAP_MAX];

bool s_check_system_capability(enum __s_system_caps cap)
{
  if( cap < SYS_CAP_MAX ) {
    return s_check_access(S_GET_INVOKER(),&system_caps[cap]);
  }
  return false;
}

void initialize_security(void)
{
  int i;

  for( i=0; i<SYS_CAP_MAX; i++ ) {
    system_caps[i].mac_label=S_INITIAL_MAC_LABEL;
  }
}

struct __task_s_object *s_clone_task_object(struct __task_struct *t)
{
  struct __task_s_object *orig,*s;

  if( !t || !(orig=t->sobject) ) {
    return NULL;
  }

  if( (s=memalloc(sizeof(*s))) ) {
    memset(s,0,sizeof(*s));

    atomic_set(&s->refcount,1);
    s->sobject=orig->sobject;
  }

  return s;
}

struct __task_s_object *s_alloc_task_object(mac_label_t label,uid_t uid)
{
  struct __task_s_object *s;

  if( (s=memalloc(sizeof(*s))) ) {
    memset(s,0,sizeof(*s));

    atomic_set(&s->refcount,1);
    s->sobject.mac_label=label;
    s->sobject.uid=uid;
  }

  return s;
}
