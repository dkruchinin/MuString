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
#include <arch/spinlock.h>
#include <sync/spinlock.h>

typedef unsigned int mac_label_t;

struct __s_object {
  mac_label_t mac_label;
  rw_spinlock_t lock;
  uid_t uid;
};

struct __task_s_object {
  atomic_t refcount;
  struct __s_object sobject;
};

#define S_LOCK_OBJECT_R(t) spinlock_lock_read(&(t)->lock)
#define S_UNLOCK_OBJECT_R(t) spinlock_unlock_read(&(t)->lock)
#define S_LOCK_OBJECT_W(t) spinlock_lock_write(&(t)->lock)
#define S_UNLOCK_OBJECT_W(t) spinlock_unlock_write(&(t)->lock)

#define s_get_task_object(o) atomic_inc(&(o)->sobject->refcount)
#define s_put_task_object(o)

#define S_MAC_OK(actor_label,object_label) ((actor_label) <= (object_label))

bool s_check_access(struct __s_object *actor,struct __s_object *obj);

enum __s_system_caps {
  SYS_CAP_ADMIN,
  SYS_CAP_IO_PORT,
  SYS_CAP_CREATE_PROCESS,
  SYS_CAP_CREATE_THREAD,
  SYS_CAP_REINCARNATE,
  SYS_CAP_TASK_EVENTS,
  SYS_CAP_IPC_CHANNEL,
  SYS_CAP_IPC_PORT,
  SYS_CAP_IPC_CONTROL,
  SYS_CAP_MAX,
};

struct __task_struct;

bool s_check_system_capability(enum __s_system_caps cap);
void initialize_security(void);

struct __task_s_object *s_clone_task_object(struct __task_struct *t);
struct __task_s_object *s_alloc_task_object(mac_label_t label,uid_t uid);
void s_copy_mac_label(struct __s_object *src, struct __s_object *dst);

#define S_MAC_LABEL_MIN 1
#define S_MAC_LABEL_MAX 65535
#define S_INITIAL_MAC_LABEL S_MAC_LABEL_MIN
#define S_KTHREAD_MAC_LABEL S_MAC_LABEL_MIN

#define S_UID_MIN 0
#define S_UID_MAX 65535
#define S_INITIAL_UID S_UID_MIN
#define S_KTHREAD_UID S_UID_MIN

/* List of syscalls that were procected by MAC label check:
 *    sys_fork
 *    sys_create_task
 *    sys_task_control

        SC_CREATE_TASK         <MAC>
        SC_TASK_CONTROL        <MAC>
#define SC_MMAP                2
        SC_CREATE_PORT         <MAC>
        SC_PORT_RECEIVE        <Not needed - part of SC_CREATE_PORT>

        SC_ALLOCATE_IOPORTS    <MAC>
        SC_FREE_IOPORTS        <MAC>
#define SC_CREATE_IRQ_ARRAY    7
#define SC_WAIT_ON_IRQ_ARRAY   8
        SC_IPC_PORT_POLL       <Not needed - part of SC_CREATE_PORT>

#define SC_NANOSLEEP           10
#define SC_SCHED_CONTROL       11
#define SC_EXIT                12
        SC_OPEN_CHANNEL        <MAC>
        SC_CLOSE_CHANNEL       <Not needed - part of SC_OPEN_CHANNEL>

        SC_CLOSE_PORT          <Not needed - part of SC_CREATE_PORT>
        SC_CONTROL_CHANNEL     <Not needed - part of SC_OPEN_CHANNEL>
#define SC_SYNC_CREATE         17
#define SC_SYNC_CONTROL        18
#define SC_SYNC_DESTROY        19

#define SC_KILL                20
#define SC_SIGNAL              21
#define SC_SIGRETURN           22
        SC_PORT_SEND_IOV_V     <Not needed - part of SC_OPEN_CHANNEL>
        SC_PORT_REPLY_IOV      <Not needed - part of SC_CREATE_PORT>

#define SC_SIGACTION           25
#define SC_THREAD_KILL         26
#define SC_SIGPROCMASK         27
#define SC_THREAD_EXIT         28
#define SC_TIMER_CREATE        29

#define SC_TIMER_CONTROL       30
#define SC_MUNMAP              31
#define SC_THREAD_WAIT         32
        SC_PORT_MSG_READ       <Not needed - part of SC_CREATE_PORT>
#define SC_KERNEL_CONTROL      34

#define SC_TIMER_DELETE        35
#define SC_SIGWAITINFO         36
#define SC_SCHED_YIELD         37
#define SC_MEMOBJ_CREATE       38
#define SC_FORK                39

#define SC_GRANT_PAGES         40
#define SC_WAITPID             41
#define SC_ALLOC_DMA           42
#define SC_FREE_DMA            43
        SC_PORT_CONTROL        <MAC>

        SC_PORT_MSG_WRITE      <Not needed - part of SC_CREATE_PORT>


 *
 *
 *
 */

/* Control operations for managing MAC labels from userland */
#define S_MAC_CTL_SET_LABEL  0

struct __mac_ctl_arg {
};

#endif
