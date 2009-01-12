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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/exit.c: Task exit functionality.
 */

#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <eza/smp.h>
#include <eza/process.h>
#include <eza/kernel.h>
#include <eza/task.h>
#include <ipc/ipc.h>
#include <eza/security.h>
#include <eza/tevent.h>
#include <eza/process.h>
#include <ipc/gen_port.h>

#define __set_exiting_flag(exiter)              \
  LOCK_TASK_STRUCT((exiter));                   \
  set_task_flags((exiter),TF_EXITING);          \
  UNLOCK_TASK_STRUCT((exiter))

#define __clear_exiting_flag(exiter)            \
  LOCK_TASK_STRUCT((exiter));                   \
  clear_task_flag((exiter),TF_EXITING);          \
  UNLOCK_TASK_STRUCT((exiter))

static void __exit_ipc(task_t *exiter) {
  task_ipc_t *ipc;
  task_ipc_priv_t *p;  

  if( exiter->ipc ) {
    close_ipc_resources(exiter->ipc);
  }

  LOCK_TASK_MEMBERS(exiter);
  ipc=exiter->ipc;
  p=exiter->ipc_priv;
  exiter->ipc=NULL;
  exiter->ipc_priv=NULL;
  UNLOCK_TASK_MEMBERS(exiter);

  if( ipc ) {
    release_task_ipc(ipc);
  }

  if( p ) {
    release_task_ipc_priv(p);
  }
}

static void __exit_scheduler(task_t *exiter)
{
  sched_del_task(exiter);
}

static void __exit_limits(task_t *exiter)
{
}

static void __exit_mm(task_t *exiter)
{
}

static void __notify_disintegration_done(disintegration_descr_t *dreq,
                                         ulong_t status)
{
  disintegration_req_packet_t *p=ipc_message_data(dreq->msg);

  p->pid=current_task()->pid;
  p->status=status;

  ipc_port_send_iov(dreq->port,dreq->msg,false,NULL,0,0);
  __ipc_put_port(dreq->port);
  memfree(dreq);
}

static void __flush_pending_uworks(task_t *exiter)
{
  disintegration_descr_t *dreq=exiter->uworks_data.disintegration_descr;

  if( dreq ) { /* Pending disintegration requests ? */
    __notify_disintegration_done(dreq,__DR_EXITED);
  }
}

static void __exit_resources(task_t *exiter)
{
  /* Remove all our listeners. */
  exit_task_events(exiter);
}

void do_exit(int code,ulong_t flags)
{
  task_t *exiter=current_task();

  if( !exiter->pid ) {
    panic( "do_exit(): Exiting from the idle task on CPU N%d !\n",
           cpu_id() );
  }

  if( exiter->pid == 1 && exiter->tid == 1 ) {
    panic( "do_exit(): Exiting from the Name Server on CPU N%d !\n",
           cpu_id() );
  }

  if( in_interrupt() ) {
    panic( "do_exit(): Exiting in interrupt context on CPU N%d !\n",
           cpu_id() );
  }

  if( !(flags & EF_DISINTEGRATE) ) {
    /* It's good to be undead ! */
    zombify_task(exiter);
  } else {
    /* It's good to be just born ! */
    LOCK_TASK_STRUCT(exiter);
    exiter->state=TASK_STATE_JUST_BORN;
    UNLOCK_TASK_STRUCT(exiter);
  }

  __set_exiting_flag(exiter);

  /* Flush any pending uworks. */
  if( !(flags & EF_DISINTEGRATE) ) {
    __flush_pending_uworks(exiter);
  }

  /* Notify listeners. */
  if( !(flags & EF_DISINTEGRATE) ) {
    task_event_notify(TASK_EVENT_TERMINATION);
    __exit_limits(exiter);
  }

  __exit_ipc(exiter);
  __exit_mm(exiter);

  if( !is_thread(exiter) ) {
    if( flags & EF_DISINTEGRATE ) {
    } else {
    }
  } else {
    kprintf("** [0x%X] Thread has exited !\n",
            exiter->tid);
  }

  __exit_resources(exiter);

  if( !(flags & EF_DISINTEGRATE) ) {
    __exit_scheduler(exiter);
    panic( "do_exit(): zombie task <%d:%d> is still running !\n",
           exiter->pid, exiter->tid);
  } else {
    __clear_exiting_flag(exiter);
    __notify_disintegration_done(exiter->uworks_data.disintegration_descr,0);
    for(;;);
  }
}

void sys_exit(int code)
{
  do_exit(code,0);
}

void sys_thread_exit(int code)
{
}

void perform_disintegrate_work(void)
{
  kprintf("** DISINTEGRATING TASK %d\n",current_task()->pid);
  do_exit(0,EF_DISINTEGRATE);
}
