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
#include <eza/arch/mm_types.h>
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
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <eza/arch/spinlock.h>

#define TEST_ID "Userspace synchronization tests"

#define SERVER_ID "[Usync test] "

typedef struct __usync_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
  ulong_t mutex_id;
} usync_test_ctx_t;

#define MUTEX_CLIENT_ID "[Mutex client] "

static void __mutex_client(void *d)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t *)d;
  test_framework_t *tf=tctx->tf;
  status_t r,mutex1;

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
  status_t mutex1,r;
  task_t *task;
  uint64_t target_tick;
  
  tf->printf( "Trying to create a mutex.\n" );
  mutex1=sys_sync_create_object(__SO_MUTEX,0);
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

static void __test_thread(void *d)
{
  usync_test_ctx_t *tctx=(usync_test_ctx_t*)d;

  tctx->tf->printf(SERVER_ID "Calling mutex tests.\n");
  __mutex_test(tctx);

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

  f->printf( "Userspace tests currently deactivated !\n" );
  return;
  
  if( kernel_thread(__test_thread,tctx,NULL) ) {
    f->printf( "Can't create main test thread !" );
    f->abort();
  } else {
    tctx->tests_finished=false;
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
