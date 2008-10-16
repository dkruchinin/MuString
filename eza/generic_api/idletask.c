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
#include <eza/arch/asm.h>
#include <eza/arch/preempt.h>
#include <kernel/syscalls.h>
#include <eza/uinterrupt.h>

task_t *idle_tasks[MAX_CPUS];

#define STEP 200
#define TICKS_TO_WAIT 300

ulong_t syscall_counter = 0;

task_t *server_task;
status_t server_port;

static char counters[512] __attribute__((aligned(PAGE_SIZE)));

#define wait_ticks(x,y)

static void __wait_ticks(ulong_t n, char *s)
{
  uint64_t target_tick = swks.system_ticks_64 + n;

  kprintf( "wait_ticks(): %s\n",s );
  while(swks.system_ticks_64 < target_tick) {
  }
}

static void thread2(void *data)
{
  status_t r;
  char *server_reply="Yes, I am here. How can I help you ?";
  ulong_t reply_len=strlen(server_reply)+1;
  port_msg_info_t rcv_stats;
  char buf[64];

  server_port = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                                IPC_DEFAULT_PORT_MESSAGES);
  server_task = current_task();

  while( 1 ) {
    memset(buf,0,sizeof(buf));
    kprintf( "[Server]: Extracting a message ...\n" );
    r=ipc_port_receive(current_task(), server_port, IPC_BLOCKED_ACCESS,
                       (ulong_t)buf,sizeof(buf),&rcv_stats);
    kprintf( "[Server]: Done ! %d\n",r );
    if( !r ) {
      kprintf( "[Server]: %s [msg id: %d]\n",buf,rcv_stats.msg_id);
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
    __wait_ticks(300, "Server");
  }
}

static void thread4(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there [2]?";
  ulong_t snd_len=strlen(test_data)+1;

  while(1) {
    memset(_buf,0,sizeof(_buf));
    kprintf( "[Client %d]: Sending data to server. ATOMIC: %d\n",
             current_task()->pid, in_atomic() );
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client %d]: Data was sent: %d, ATOMIC: %d\n",
             current_task()->pid, r, in_atomic() );
    if( r >=0 ) {
        kprintf( "[Client %d]: %s\n", current_task()->pid, _buf );
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Client N2]: Can't receive response from server ! %d\n", r );      
    }
    wait_ticks(TICKS_TO_WAIT, "2");
  }
  for(;;);
}

static void thread5(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there [3]?";
  ulong_t snd_len=strlen(test_data)+1;

  while(1) {
    memset(_buf,0,sizeof(_buf));
    kprintf( "[Client %d]: Sending data to server. ATOMIC: %d\n",
             current_task()->pid, in_atomic() );
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client %d]: Data was sent: %d, ATOMIC: %d\n",
             current_task()->pid, r, in_atomic() );
    if( r >=0 ) {
      kprintf( "[Client %d]: %s\n", current_task()->pid,_buf );
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Client N3]: Can't receive response from server ! %d\n", r );      
    }
    wait_ticks(TICKS_TO_WAIT, "3");
  }
  for(;;);
}

static void thread6(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there [4]?";
  ulong_t snd_len=strlen(test_data)+1;

  while(1) {
    memset(_buf,0,sizeof(_buf));

    kprintf( "[Client N4]: Sending data to server...\n" );
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client N4]: Data was sent: %d\n", r );
    if( r >=0 ) {
      kprintf( "[Client N4]: %s\n", _buf );
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Client N4]: Can't receive response from server ! %d\n", r );      
    }
    wait_ticks(TICKS_TO_WAIT, "4");
  }
  for(;;);
}


static void thread3(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there [1]?";
  ulong_t snd_len=strlen(test_data)+1;

  kprintf( "Creating client N2 ...\n" );
  if( kernel_thread(thread4,NULL) != 0 ) {
    panic( "Can't create client thread N2 for testing port IPC functionality !\n" );
  }

  kprintf( "Creating client N3 ...\n" );
  if( kernel_thread(thread5,NULL) != 0 ) {
    panic( "Can't create client thread N3 for testing port IPC functionality !\n" );
  }

  sched_change_task_state(current_task(), TASK_STATE_STOPPED);
  for(;;);
  
  kprintf( "Creating client N4 ...\n" );
  if( kernel_thread(thread6,NULL) != 0 ) {
    panic( "Can't create client thread N4 for testing port IPC functionality !\n" );
  }

  while(1) {
    memset(_buf,0,sizeof(_buf));
    kprintf( "[Client N1]: Sending data to server...\n" );
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client N1]: Data was sent: %d\n", r );
    if( r >=0 ) {
      kprintf( "[Client N1]: %s\n", _buf );
    } else {
      __asm__  __volatile__( "cli" );
      panic( "[Client N1]: Can't receive response from server ! %d\n", r );      
    }
    wait_ticks(TICKS_TO_WAIT, "1" );
  }
  for(;;);
}

static void ioport_thread(void *data)
{
    ulong_t p1,l;
    
    kprintf( "ioport thread: Starting ...\n" );
    p1=0x31;
    l=0x5;
    kprintf( "Trying to allocate IO ports [%d,%d]: %d\n",
             p1,l,sys_allocate_ioports(p1,l) );
    for(;;);
}

status_t sys_wait_on_irq_array(ulong_t id);
status_t sys_create_irq_counter_array(ulong_t irq_array,ulong_t irqs,
                                      ulong_t addr,ulong_t flags);

void interrupt_thread(void *data)
{
    ulong_t irqs[]={10,6,1,4};
    static int num_irqs=4;
    status_t id=sys_create_irq_counter_array(irqs,num_irqs,counters,0);

    kprintf( "IRQ buffer id: %d\n",id );
    
    if( id >= 0 ) {
        int i=0;
        status_t r;
        char code;

        code=inb(0x60);
        while( i < 5000000 ) {
            kprintf( "Waiting for irqs to arrive ...\n" );
            r=sys_wait_on_irq_array(id);
            if( !r ) {
                code=inb(0x60);
                kprintf( "CNT: %d, KEY: %d\n", counters[2], (int)code );
//                counters[2]--;
            }
            i++;
        }
    }

    for(;;);
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  if( cpu_id() == 0 ) {
      if( kernel_thread(interrupt_thread,NULL) != 0 ) {
          panic( "Can't create server thread for testing port IPC functionality !\n" );
      }
  }
  
/*
  if( cpu_id() == 0 ) {
    if( kernel_thread(ioport_thread,NULL) != 0 ) {
      panic( "Can't create server thread for testing port IPC functionality !\n" );
    }

    if( kernel_thread(thread2,NULL) != 0 ) {
      panic( "Can't create server thread for testing port IPC functionality !\n" );
    }
    if( kernel_thread(thread3,NULL) != 0 ) {
      panic( "Can't create client thread for testing port IPC functionality !\n" );
      }
  }
*/
  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Idle #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d), SYSCALLS: %d\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic(),
               syscall_counter );
      target_tick += STEP;
    }
  }
}

