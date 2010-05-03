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
#include <sync/spinlock.h>

typedef unsigned int mac_label_t;

struct __s_creds {
  mac_label_t mac_label;
  uid_t uid;
  gid_t gid;
};

struct __s_object {
  struct __s_creds creds;
  rw_spinlock_t lock;
};

struct __task_s_object {
  atomic_t refcount;
  struct __s_object sobject;
};

#define S_INIT_OBJECT(obj) do {                 \
    rw_spinlock_initialize(&(obj)->lock,"MAC"); \
  } while(0)

#define S_LOCK_OBJECT_R(t) spinlock_lock_read(&(t)->lock)
#define S_UNLOCK_OBJECT_R(t) spinlock_unlock_read(&(t)->lock)
#define S_LOCK_OBJECT_W(t) spinlock_lock_write(&(t)->lock)
#define S_UNLOCK_OBJECT_W(t) spinlock_unlock_write(&(t)->lock)

#define s_get_task_object(o) atomic_inc(&(o)->sobject->refcount)
#define s_put_task_object(o)

/* Abstract functions for MACs comparison. */
#define S_MACS_WE(mac1,mac2)  ((mac1) >= (mac2)) /* Weaker or equal */
#define S_MACS_SE(mac1,mac2)  ((mac1) <= (mac2)) /* Stronger or equal */
#define S_MACS_EQ(mac1,mac2)  ((mac1) == (mac2)) /* Equal */

#define S_MAC_OK(actor_label,object_label) (S_MACS_SE(actor_label,object_label))

bool s_check_access(struct __s_object *actor,struct __s_object *obj);
bool s_get_greds();

/* System capabilities. */
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
  SYS_CAP_IRQ,
  SYS_CAP_SCHEDULER,
  SYS_CAP_SYNC,
  SYS_CAP_TIMER,
  SYS_CAP_REMOTE_MAP,
  SYS_CAP_MEMOBJ,
  SYS_CAP_DMA,
  SYS_CAP_PTRACE,
  SYS_CAP_MAX, /* Must be last. */
};

struct __task_struct;

bool s_check_system_capability(enum __s_system_caps cap);
void initialize_security(void);

enum s_cred_type {
  S_CRED_WHOLE,
  S_CRED_MAC_LABEL,
  S_CRED_UID,
};

struct __task_s_object *s_clone_task_object(struct __task_struct *t);
struct __task_s_object *s_alloc_task_object(mac_label_t label,uid_t uid,gid_t gid);
void s_copy_mac_label(struct __s_object *src, struct __s_object *dst);
long s_set_obj_cred(struct __s_object *target, enum s_cred_type cred,
                    struct __s_creds *new);
long s_get_obj_cred(struct __s_object *target, enum s_cred_type cred,
                    struct __s_creds *out);
long s_get_sys_mac_label(ulong_t cap,mac_label_t *out);
long s_set_sys_mac_label(ulong_t cap,mac_label_t *in);

#define S_MAC_LABEL_MIN 1
#define S_MAC_LABEL_MAX 65535
#define S_INITIAL_MAC_LABEL S_MAC_LABEL_MIN
#define S_KTHREAD_MAC_LABEL S_MAC_LABEL_MIN

#define S_UID_MIN 0
#define S_UID_MAX 65535
#define S_GID_MIN 0
#define S_GID_MAX 65535

#define S_INITIAL_UID S_UID_MIN
#define S_KTHREAD_UID S_UID_MIN
#define S_KTHREAD_GID S_GID_MIN

#define S_MAC_LABEL_VALID(label) (((label) >= S_MAC_LABEL_MIN) && ((label) <= S_MAC_LABEL_MAX))
#define S_UID_VALID(uid) ((uid) < S_UID_MAX)

static inline void s_get_obj_creds(struct __s_object *obj, struct __s_creds *creds)
{
  S_LOCK_OBJECT_R(obj);
  *creds=obj->creds;
  S_UNLOCK_OBJECT_R(obj);
}

/* List of syscalls that were procected by MAC label check:

        SC_CREATE_TASK         <MAC>
        SC_TASK_CONTROL        <MAC>
        SC_MMAP                <MAC>
        SC_CREATE_PORT         <MAC>
        SC_PORT_RECEIVE        <Not needed - part of SC_CREATE_PORT>

        SC_ALLOCATE_IOPORTS    <MAC>
        SC_FREE_IOPORTS        <MAC>
        SC_CREATE_IRQ_ARRAY    <MAC>
        SC_WAIT_ON_IRQ_ARRAY   <Not needed - part of SC_CREATE_IRQ_ARRAY>
        SC_IPC_PORT_POLL       <Not needed - part of SC_CREATE_PORT>

        SC_NANOSLEEP           <Not needed>
        SC_SCHED_CONTROL       <MAC>
        SC_EXIT                <Not needed>
        SC_OPEN_CHANNEL        <MAC>
        SC_CLOSE_CHANNEL       <Not needed - part of SC_OPEN_CHANNEL>

        SC_CLOSE_PORT          <Not needed - part of SC_CREATE_PORT>
        SC_CONTROL_CHANNEL     <Not needed - part of SC_OPEN_CHANNEL>
        SC_SYNC_CREATE         <MAC>
        SC_SYNC_CONTROL        <Not needed - part of SC_SYNC_CREATE>
        SC_SYNC_DESTROY        <Not needed - part of SC_SYNC_CREATE>

        SC_KILL                <MAC>
        SC_SIGNAL              <Not needed - part of SC_KILL>
        SC_SIGRETURN           <>
        SC_PORT_SEND_IOV_V     <Not needed - part of SC_OPEN_CHANNEL>
        SC_PORT_REPLY_IOV      <Not needed - part of SC_CREATE_PORT>

        SC_SIGACTION           <Not needed - part of SC_KILL>
        SC_THREAD_KILL         <MAC>
        SC_SIGPROCMASK         <>
        SC_THREAD_EXIT         <>
        SC_TIMER_CREATE        <MAC>

        SC_TIMER_CONTROL       <Not needed - part of SC_TIMER_CREATE>
        SC_MUNMAP              <MAC>
        SC_THREAD_WAIT         <Not needed - in-process action only>
        SC_PORT_MSG_READ       <Not needed - part of SC_CREATE_PORT>
        SC_KERNEL_CONTROL      <MAC>

        SC_TIMER_DELETE        <Not needed - part of SC_TIMER_CREATE>
        SC_SIGWAITINFO         <>
        SC_SCHED_YIELD         <>
        SC_MEMOBJ_CREATE       <MAC>
        SC_FORK                <MAC>

        SC_GRANT_PAGES         <MAC>
        SC_WAITPID             <MAC>
#define SC_ALLOC_DMA           42
#define SC_FREE_DMA            43
        SC_PORT_CONTROL        <MAC>

        SC_PORT_MSG_WRITE      <Not needed - part of SC_CREATE_PORT>
        SC_SECURITY_CONTROL    <MAC>
        SC_PTRACE              <MAC>

        SC_SIGSUSPEND          <>
 *
 *
 *
 */

/* Control operations for managing MAC labels from userland.
 */
#define S_MAC_CTL_SET_LABEL    0
#define S_MAC_CTL_GET_CREDS    1
#define S_MAC_CTL_SET_UID      2
#define S_MAC_CTL_GET_SYS_CAP  3
#define S_MAC_CTL_SET_SYS_CAP  4
#define S_MAC_CTL_SET_PORT_LABEL 5
#define S_MAC_CTL_GET_PORT_LABEL 6
#define S_MAX_CTL_LAST_CMD  S_MAC_CTL_GET_PORT_LABEL

struct __mac_ctl_arg {
  struct __s_creds creds;
  union {
    unsigned long port;
  } target_obj;
};

#endif
