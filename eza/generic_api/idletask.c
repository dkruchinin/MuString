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

static void thread2(void *data)
{
    status_t id,r;
    char buf[32];

    kprintf( "[Server] Strting ...\n" );
    id = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                         IPC_DEFAULT_PORT_MESSAGES);
    kprintf( "[Server] Creating a port. id = %d\n",id );

    server_task = current_task();
    server_port = id;

    kprintf( "[Server] Waitng for incoming messages ...\n" );
    r = ipc_port_receive(current_task(), id, IPC_BLOCKED_ACCESS,buf,32);
    kprintf( "[Server] Got a message: %d\n", r );
    kprintf( "[Server] Replying message N%d\n", r );
    r = ipc_port_reply(current_task(), id, r, buf, 32);
    kprintf( "r = %d\n", r );

    for(;;);
}

static void thread3(void *data)
{
  status_t r;
  char buf[32];

  kprintf( "[Client] Starting ...\n" );
  r = ipc_port_send(server_task,server_port,32,32,IPC_BLOCKED_ACCESS);
  kprintf( "[Client] Data was sent through the port: %d\n", r );
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

