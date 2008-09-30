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

#define STEP 300

static void thread1(void *data) {
    status_t id,r;

    kprintf( "** Creating a port ... " );
    id = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                         IPC_DEFAULT_PORT_MESSAGES);
    kprintf( "port id: %d\n", id );

    kprintf( "** Opening the port ... " );
    r = ipc_open_port(current_task(),id,IPC_BLOCKED_ACCESS,current_task());
    kprintf( "port descriptor: %d\n", r );

    kprintf( "** Opening insufficient port port (1) : %d\n",
             ipc_open_port(current_task(),1,IPC_BLOCKED_ACCESS,current_task()) );

    kprintf( "** Opening the port one more time ... " );
    r = ipc_open_port(current_task(),id,IPC_BLOCKED_ACCESS,current_task());
    kprintf( "port id: %d\n", r );

    kprintf( "** Sending a message to the port ... " );
    r = ipc_port_send(current_task(),id,32,32,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Sending another message to the port ...\n" );
    r = ipc_port_send(current_task(),id,32,32,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Receiving a message from the port ... " );
    r = ipc_port_receive(current_task(),id,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Receiving another message from the port ... " );
    r = ipc_port_receive(current_task(),id,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Receiving the third message from the port ... " );
    r = ipc_port_receive(current_task(),id,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Sending a message to the port ... " );
    r = ipc_port_send(current_task(),id,32,32,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Sending a message to the port ... " );
    r = ipc_port_send(current_task(),id,32,32,0);
    kprintf( "r = %d\n",r );
    
    kprintf( "** Receiving a message from the port ... " );
    r = ipc_port_receive(current_task(),id,0);
    kprintf( "r = %d\n",r );

    kprintf( "** Receiving a message from the port ... " );
    r = ipc_port_receive(current_task(),id,0);
    kprintf( "r = %d\n",r );
    
    for(;;);
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  if( kernel_thread(thread1,NULL) != 0 ) {
      panic( "Can't create thread for testing port IPC functionality !\n" );
  }
  
  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Idle #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      target_tick += STEP;
    }
  }
}

