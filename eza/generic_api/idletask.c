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
 * eza/generic_api/idletask.c: generic idle tasks-related functions.
 */

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

task_t *idle_tasks[MAX_CPUS];

#define STEP 5000

ulong_t syscall_counter = 0;

task_t *server_task;
status_t server_port;

static void wait_ticks(ulong_t n)
{
  uint64_t target_tick = swks.system_ticks_64 + n;

  while(swks.system_ticks_64 < target_tick) {
  }
}

static void thread2(void *data)
{
  status_t r;
  char *server_reply="Yes, I am here. How can I help you ?";
  ulong_t reply_len=strlen(server_reply)+1;
  ipc_port_receive_stats_t rcv_stats;
  char buf[64];

  server_port = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                                IPC_DEFAULT_PORT_MESSAGES);
  server_task = current_task();

  while( 1 ) {
    memset(buf,0,sizeof(buf));
    r=ipc_port_receive(current_task(), server_port, IPC_BLOCKED_ACCESS,
                       (ulong_t)buf,sizeof(buf),&rcv_stats);

    if( !r ) {
      kprintf( "[Server]: %s\n" );
      r=ipc_port_reply(current_task(),server_port,rcv_stats.msg_id,
                       (uintptr_t)server_reply,reply_len);
      if( r<0 ) {
        __asm__  __volatile__( "cli" );
        panic( "[Server]: Can't reply to client ! %d\n", r );
      }
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Server]: Can't receive data from port ! %d\n", r );
    }
  }
}

static void thread3(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there ?";
  ulong_t snd_len=strlen(test_data)+1;

  while(1) {
    memset(_buf,0,sizeof(_buf));
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    if( r >=0 ) {
      kprintf( "[Client]: %s\n", _buf );
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Client]: Can't receive response from server ! %d\n", r );      
    }
    wait_ticks(100);
  }
  for(;;);
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  if( cpu_id() == 0 ) {
      /* Start server */
      if( kernel_thread(thread2,NULL) != 0 ) {
          panic( "Can't create server thread for testing port IPC functionality !\n" );
      }
      /* Start client */
      if( kernel_thread(thread3,NULL) != 0 ) {
          panic( "Can't create client thread for testing port IPC functionality !\n" );
      }
  }

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Idle #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d), SYSCALLS: %d\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic(),
               syscall_counter );
      target_tick += STEP;
    }
  }
}

