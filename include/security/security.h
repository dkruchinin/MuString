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
 * include/security/security.h: Common data types and function prototypes for
 *                              the kernel security facility.
 */

#ifndef __SECURITY_H__
#define __SECURITY_H__

#include <mstring/types.h>
#include <arch/atomic.h>

typedef unsigned int mac_label_t;

struct __s_object {
  mac_label_t mac_label;
  uid_t uid;
};

struct __task_s_object {
  atomic_t refcount;
  struct __s_object sobject;
};

#define s_get_task_object(o) atomic_inc(&(o)->sobject->refcount)
#define s_put_task_object(o)

#define S_MAC_OK(actor_label,object_label) ((actor_label) <= (object_label))

static inline bool s_check_access(struct __s_object *actor,
                                  struct __s_object *obj)
{
  if( S_MAC_OK(actor->mac_label,obj->mac_label) ) {
    return true;
  }
  return false;
}

enum __s_system_caps {
  SYS_CAP_ADMIN=0,
  SYS_CAP_IO_PORT_ALLOC,
  SYS_CAP_CREATE_PROCESS,
  SYS_CAP_CREATE_THREAD,
  SYS_CAP_MAX,
};

struct __task_struct;

bool s_check_system_capability(enum __s_system_caps cap);
void initialize_security(void);

struct __task_s_object *s_clone_task_object(struct __task_struct *t);
struct __task_s_object *s_alloc_task_object(mac_label_t label,
                                            uid_t uid);

#define S_MAC_LABEL_MIN 1
#define S_MAC_LABEL_MAX 65535
#define S_INITIAL_MAC_LABEL S_MAC_LABEL_MIN
#define S_KTHREAD_MAC_LABEL S_MAC_LABEL_MIN

#define S_UID_MIN 0
#define S_UID_MAX 65535
#define S_INITIAL_UID S_UID_MIN
#define S_KTHREAD_UID S_UID_MIN

#endif
