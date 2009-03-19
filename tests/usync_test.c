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
 * test/sched_test.c: tests for Muistring scheduler subsystem.
 */

#include <config.h>
#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/preempt.h>
#include <eza/spinlock.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <eza/arch/asm.h>
#include <eza/arch/preempt.h>
#include <kernel/syscalls.h>
#include <eza/uinterrupt.h>
#include <ipc/poll.h>
#include <eza/gc.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <eza/arch/spinlock.h>
#include <eza/arch/profile.h>

#define TEST_ID "Userspace synchronization tests"

#define SERVER_ID "[Usync test] "

typedef struct __usync_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
  ulong_t mutex_id;
  sync_id_t event_id;
} usync_test_ctx_t;

#define MUTEX_CLIENT_ID "[Mutex client] "

static void __mutex_client(void *d)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t *)d;
  test_framework_t *tf=tctx->tf;
  int r,mutex1;

  mutex1=tctx->mutex_id;

  tf->printf( MUTEX_CLIENT_ID "Trying to try to lock a locked mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_TRYLOCK,0);
  if( r != -EBUSY ) {
    tf->printf( MUTEX_CLIENT_ID "Failed to try to lock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( MUTEX_CLIENT_ID "Trying to lock the mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_LOCK,0);
  if( r ) {
    tf->printf( MUTEX_CLIENT_ID "Can't lock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->printf(MUTEX_CLIENT_ID "Successfully grabbed the mutex !\n" );
    tf->passed();
  }

  tf->printf( MUTEX_CLIENT_ID "Trying to unlock the mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_UNLOCK,0);
  if( r ) {
    tf->printf( MUTEX_CLIENT_ID "Can't unlock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( MUTEX_CLIENT_ID "All subtests passed.\n" );
  sys_exit(0);
}

static void __mutex_test(usync_test_ctx_t *tctx)
{
  test_framework_t *tf=tctx->tf;
  int mutex1,r;
  task_t *task;
  uint64_t target_tick;
  
  tf->printf( "Trying to create a mutex.\n" );
//  mutex1=sys_sync_create_object(__SO_MUTEX,0);
  if( mutex1 <= 0 ) {
    tf->printf( "Can't create a mutex: %d\n",mutex1 );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( "Trying to lock the mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_LOCK,0);
  if( r ) {
    tf->printf( "Can't lock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( "Trying to try to lock a locked mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_TRYLOCK,0);
  if( r != -EBUSY ) {
    tf->printf( "Failed to try to lock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( "Trying to unlock the mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_UNLOCK,0);
  if( r ) {
    tf->printf( "Can't unlock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( "Trying to try to lock a free mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_TRYLOCK,0);
  if( r ) {
    tf->printf( "Failed to try to lock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tctx->mutex_id=mutex1;
  if( kernel_thread(__mutex_client,tctx,&task) ) {
    tf->printf( "Can't create a client thread !\n" );
    tf->abort();
  }

  tf->printf( "Sleeping for a while ...\n" );
  sleep(HZ/2);
  tf->printf( "Got woken up !\n" );

  tf->printf( "Trying to unlock the mutex.\n" );
  r=sys_sync_control(mutex1,__SYNC_CMD_MUTEX_UNLOCK,0);
  if( r ) {
    tf->printf( "Can't unlock the mutex: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tf->printf( "Sleeping for a while ...\n" );
  sleep(HZ/200);
  tf->printf( "Got woken up !\n" );
}

ulong_t __ecount=0;

#define UEVENT_ROUNDS 50

static void __uevent_client( void *d)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t *)d;
  test_framework_t *tf=tctx->tf;
  ulong_t _ts;
  int r,i;

  tf->printf( "Event client is starting.\n" );
  if( sched_move_task_to_cpu(current_task(),1) ) {
    tf->printf( "Can't move to CPU #1 !\n" );
    tf->failed();
  }

  for(i=0;i<UEVENT_ROUNDS;i++) {
    __READ_TIMESTAMP_COUNTER(_ts);
    sleep(_ts % HZ/5);
    tf->printf( "Signalling event.\n" );
    __ecount=200;
    r=sys_sync_control(tctx->event_id,__SYNC_CMD_EVENT_SIGNAL,0);
    if( r ) {
      tf->printf( "Can't signal event: %d\n",r );
      tf->failed();
    } else {
      tf->passed();
    }
  }
  sys_exit(0);
}

static void __uevent_tests(usync_test_ctx_t *tctx)
{
  test_framework_t *tf=tctx->tf;
  sync_id_t ev_id;
  int i,r;

  tf->printf( "Trying to create an event.\n" );
  r=sys_sync_create_object(__SO_RAWEVENT,&ev_id,NULL,0);
  if( r ) {
    tf->printf( "Can't create event: %d\n",r );
    tf->failed();
  } else {
    tf->passed();
  }

  tctx->event_id=ev_id;
  if( kernel_thread(__uevent_client,tctx,NULL) ) {
    tf->printf( "Can't create a uevent client thread !\n" );
    tf->abort();
  }

  for(i=0;i<UEVENT_ROUNDS;i++) {
    tf->printf("* Waiting for event.\n");
    r=sys_sync_control(ev_id,__SYNC_CMD_EVENT_WAIT,0);
    if( r ) {
      tf->printf("Event occured while waiting for event: %d\n",r);
      tf->failed();
    } else {
      tf->printf("Event already arrived !\n");
      if( __ecount == 200 ) {
        __ecount=0;
        tf->passed();
      } else {
        tf->printf("Producer produced insufficient product ! %d:200\n",
                   __ecount);
        tf->failed();
      }
    }
  }
}

static void __test_thread(void *d)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t*)d;

  tctx->tf->printf( "Calling raw events tests.\n" );
  __uevent_tests(tctx);

  //tctx->tf->printf(SERVER_ID "Calling mutex tests.\n");
  //__mutex_test(tctx);

  tctx->tf->printf(SERVER_ID "All userspace synchronization tests finished.\n");
  tctx->tests_finished=true;
  sys_exit(0);
}

static bool __usync_tests_initialize(void **ctx)
{
  usync_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    return true;
  }

  return false;
}

void __usync_tests_run(test_framework_t *f,void *ctx)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t*)ctx;
  tctx->tf=f;

  tctx->tests_finished=false;
  if( kernel_thread(__test_thread,tctx,NULL) ) {
    f->printf( "Can't create main test thread !" );
    f->abort();
  } else {
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}

void __usync_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t usync_testcase={
  .id=TEST_ID,
  .initialize=__usync_tests_initialize,
  .deinitialize=__usync_tests_deinitialize,
  .run=__usync_tests_run,
};
