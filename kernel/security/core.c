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
#include <sync/spinlock.h>
#include <mstring/task.h>
#include <arch/current.h>
#include <security/util.h>
#include <mm/slab.h>
#include <arch/atomic.h>
#include <mstring/process.h>

static struct __s_object system_caps[SYS_CAP_MAX];

#define __S_LOCK_OBJS_RR(actor,obj) do {         \
  if( (actor) != (obj) ) {                       \
    if( (actor) > (obj) ) {                      \
      S_LOCK_OBJECT_R((actor));                  \
      S_LOCK_OBJECT_R((obj));                    \
    } else {                                     \
      S_LOCK_OBJECT_R((obj));                    \
      S_LOCK_OBJECT_R((actor));                  \
    }                                            \
  } else {                                       \
    S_LOCK_OBJECT_R((actor));                    \
  }                                              \
  } while(0)

#define __S_UNLOCK_OBJS_RR(actor,obj) do {      \
  if( (actor) != (obj) ) {                      \
    if( (actor) < (obj) ) {                     \
      S_UNLOCK_OBJECT_R((actor));               \
      S_UNLOCK_OBJECT_R((obj));                 \
    } else {                                    \
      S_UNLOCK_OBJECT_R((obj));                 \
      S_UNLOCK_OBJECT_R((actor));               \
    }                                           \
  } else {                                      \
    S_UNLOCK_OBJECT_R((actor));                 \
  }                                             \
  } while(0)

#define __S_LOCK_OBJS_RW(actor,obj) do {         \
  if( (actor) != (obj) ) {                       \
    if( (actor) > (obj) ) {                      \
      S_LOCK_OBJECT_R((actor));                  \
      S_LOCK_OBJECT_W((obj));                    \
    } else {                                     \
      S_LOCK_OBJECT_W((obj));                    \
      S_LOCK_OBJECT_R((actor));                  \
    }                                            \
  } else {                                       \
    S_LOCK_OBJECT_W((actor));                    \
  }                                              \
  } while(0)

#define __S_UNLOCK_OBJS_RW(actor,obj) do {      \
  if( (actor) != (obj) ) {                      \
    if( (actor) < (obj) ) {                     \
      S_UNLOCK_OBJECT_R((actor));               \
      S_UNLOCK_OBJECT_W((obj));                 \
    } else {                                    \
      S_UNLOCK_OBJECT_W((obj));                 \
      S_UNLOCK_OBJECT_R((actor));               \
    }                                           \
  } else {                                      \
    S_UNLOCK_OBJECT_W((actor));                 \
  }                                             \
  } while(0)

bool s_check_system_capability(enum __s_system_caps cap)
{
  return true;

  if( cap < SYS_CAP_MAX ) {
    return s_check_access(S_GET_INVOKER(),&system_caps[cap]);
  }
  return false;
}

void initialize_security(void)
{
  int i;

  for( i=0; i<SYS_CAP_MAX; i++ ) {
    rw_spinlock_initialize(&system_caps[i].lock, "MAC");
    system_caps[i].creds.mac_label=S_INITIAL_MAC_LABEL;
  }
}

struct __task_s_object *s_clone_task_object(struct __task_struct *t)
{
  struct __task_s_object *orig;
  mac_label_t l;
  uid_t uid;
  gid_t gid;

  if( !t || !(orig=t->sobject) ) {
    return NULL;
  }

  S_LOCK_OBJECT_R(&orig->sobject);
  l=orig->sobject.creds.mac_label;
  uid=orig->sobject.creds.uid;
  gid=orig->sobject.creds.gid;
  S_UNLOCK_OBJECT_R(&orig->sobject);

  return s_alloc_task_object(l,uid,gid);
}

struct __task_s_object *s_alloc_task_object(mac_label_t label,uid_t uid,gid_t gid)
{
  struct __task_s_object *s;

  if( (s=memalloc(sizeof(*s))) ) {
    memset(s,0,sizeof(*s));

    atomic_set(&s->refcount,1);
    rw_spinlock_initialize(&s->sobject.lock, "MAC");
    s->sobject.creds.mac_label=label;
    s->sobject.creds.uid=uid;
    s->sobject.creds.gid=gid;
  }

  return s;
}

bool s_check_access(struct __s_object *actor,struct __s_object *obj)
{
  bool can;

  return true;

  __S_LOCK_OBJS_RR(actor,obj);
  can=S_MAC_OK(actor->creds.mac_label,obj->creds.mac_label);
  __S_UNLOCK_OBJS_RR(actor,obj);

  return can;
}

void s_copy_mac_label(struct __s_object *src, struct __s_object *dst)
{
  if( src != dst ) {
    __S_LOCK_OBJS_RW(src,dst);
    dst->creds.mac_label=src->creds.mac_label;
    dst->creds.uid=src->creds.uid;
    dst->creds.gid=src->creds.gid;
    __S_UNLOCK_OBJS_RW(src,dst);
  }
}

long s_set_obj_cred(struct __s_object *target, enum s_cred_type cred,
                    struct __s_creds *new)
{
  long r=0;

  S_LOCK_OBJECT_W(target);
  switch( cred ) {
    case S_CRED_MAC_LABEL:
      if( S_MAC_LABEL_VALID(new->mac_label) ) {
        target->creds.mac_label=new->mac_label;
      } else {
        r=-EINVAL;
      }
      break;
    case S_CRED_UID:
      if( S_UID_VALID(new->uid) ) {
        target->creds.uid=new->uid;
      } else {
        r=-EINVAL;
      }
      break;
    case S_CRED_WHOLE:
      if( S_MAC_LABEL_VALID(new->mac_label) &&
          S_UID_VALID(new->uid) ) {
        target->creds=*new;
      } else {
        r=-EINVAL;
      }
      break;
    default:
      r=-EINVAL;
      break;
  }
  S_UNLOCK_OBJECT_W(target);
  return ERR(r);
}

long s_get_obj_cred(struct __s_object *target, enum s_cred_type cred,
                    struct __s_creds *out)
{
  long r=0;

  S_LOCK_OBJECT_R(target);
  switch( cred ) {
    case S_CRED_MAC_LABEL:
      out->mac_label=target->creds.mac_label;
      break;
    case S_CRED_UID:
      out->uid=target->creds.uid;
      break;
    case S_CRED_WHOLE:
      *out=target->creds;
      break;
    default:
      r=-EINVAL;
      break;
  }
  S_UNLOCK_OBJECT_R(target);
  return ERR(r);
}

long s_get_sys_mac_label(ulong_t cap,mac_label_t *out)
{
  if( cap < SYS_CAP_MAX) {
    S_LOCK_OBJECT_R(&system_caps[cap]);
    *out=system_caps[cap].creds.mac_label;
    S_UNLOCK_OBJECT_R(&system_caps[cap]);
    return 0;
  }
  return -EINVAL;
}

long s_set_sys_mac_label(ulong_t cap,mac_label_t *in)
{
  if( cap < SYS_CAP_MAX) {
    S_LOCK_OBJECT_W(&system_caps[cap]);
    system_caps[cap].creds.mac_label=*in;
    S_UNLOCK_OBJECT_W(&system_caps[cap]);
    return 0;
  }
  return -EINVAL;
}
