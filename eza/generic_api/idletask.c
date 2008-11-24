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
#include <ipc/poll.h>
#include <eza/gc.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>

task_t *idle_tasks[MAX_CPUS];

#define STEP 600
#define TICKS_TO_WAIT 300

ulong_t syscall_counter = 0;

task_t *server_task;
status_t server_port,server_port2,server_port3;

static char counters[512] __attribute__((aligned(PAGE_SIZE)));

#define wait_ticks(x,y)

static void __wait_ticks(ulong_t n, char *s)
{
  uint64_t target_tick = swks.system_ticks_64 + n;

  kprintf( "wait_ticks(): %s\n",s );
  while(swks.system_ticks_64 < target_tick) {
  }
}

static void __server_process_port(ulong_t port)
{
  char buf[64];
  status_t r;
  char *server_reply="Yes, I am here. How can I help you ?";
  ulong_t reply_len=strlen(server_reply)+1;
  port_msg_info_t rcv_stats;

  memset(buf,0,sizeof(buf));
  kprintf( "[Server]: Extracting a message ...\n" );
  r=ipc_port_receive(current_task(), port, IPC_BLOCKED_ACCESS,
                     (ulong_t)buf,sizeof(buf),&rcv_stats);
  kprintf( "[Server]: Done ! %d\n",r );

  if( !r ) {
    kprintf( "[Server]: %s [msg id: %d]\n",buf,rcv_stats.msg_id);
    r=ipc_port_reply(current_task(),port,rcv_stats.msg_id,
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

static void thread2(void *data)
{
  static int NUM_PORTS=3;
  pollfd_t pfds[NUM_PORTS];
  ulong_t i;
  status_t r;

  server_port = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                                IPC_DEFAULT_PORT_MESSAGES);

  server_port2 = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                                 IPC_DEFAULT_PORT_MESSAGES);

  server_port3 = ipc_create_port(current_task(),IPC_BLOCKED_ACCESS,
                                 IPC_DEFAULT_PORT_MESSAGES);

  if( server_port < 0 || server_port2 < 0 || server_port3 < 0 ) {
    panic( "[SERVER]: Can't create 3 IPC ports !\n" );
  }

  kprintf( "[Server]: Ports created: %d, %d, %d\n",
           server_port,server_port2,server_port3);

  server_task = current_task();

  pfds[0].fd=server_port3;
  pfds[0].events=POLLIN | POLLRDNORM;

  pfds[1].fd=server_port2;
  pfds[1].events=POLLIN | POLLRDNORM;

  pfds[2].fd=server_port;
  pfds[2].events=POLLIN | POLLRDNORM;

  while( 1 ) {
    kprintf( "[Server]: Polling ports (1) ...\n" );
    r=sys_ipc_port_poll(pfds,NUM_PORTS,NULL);
    kprintf( "[Server]: After polling ports: %d\n",r );

    kprintf( "[Server]: Exiting ...\n" );
    sys_exit(0);

    for(i=0;i<NUM_PORTS;i++) {
      if(pfds[i].revents) {
        kprintf( "[Server]: Port %d has some pending events: 0x%X\n",
                 pfds[i].fd,pfds[i].revents );
        __server_process_port(pfds[i].fd);
      }
    }
    __wait_ticks(TICKS_TO_WAIT, "Server");
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
    kprintf( "[Client %d]: Sending data to server via %d. ATOMIC: %d\n",
             current_task()->pid, server_port, in_atomic() );
    r = ipc_port_send(server_task,server_port,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client %d]: Data was sent: %d, ATOMIC: %d\n",
             current_task()->pid, r, in_atomic() );
    if( r >=0 ) {
        kprintf( "[Client %d]: %s\n", current_task()->pid, _buf );
    } else {
//      __asm__  __volatile__( "cli" );
        kprintf( "[Client N2]: Can't receive response from server ! %d\n", r );
        break;
    }
    wait_ticks(TICKS_TO_WAIT, "2");
  }
  sys_exit(0);
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
    kprintf( "[Client %d]: Sending data to server via %d. ATOMIC: %d\n",
             current_task()->pid, server_port2, in_atomic() );
    r = ipc_port_send(server_task,server_port2,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client %d]: Data was sent: %d, ATOMIC: %d\n",
             current_task()->pid, r, in_atomic() );
    if( r >=0 ) {
      kprintf( "[Client %d]: %s\n", current_task()->pid,_buf );
    } else {
      __asm__  __volatile__( "cli" );
      kprintf( "[Client N3]: Can't receive response from server ! %d\n", r );
      break;
    }
    wait_ticks(TICKS_TO_WAIT, "3");
  }
  sys_exit(0);
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

    kprintf( "[Client NA]: Sending data to server via %d...\n",server_port2 );
    r = ipc_port_send(server_task,server_port2,(ulong_t)test_data,
                      snd_len,(ulong_t)_buf,sizeof(_buf));
    kprintf( "[Client NA]: Data was sent: %d\n", r );
    if( r >=0 ) {
      kprintf( "[Client NA]: %s\n", _buf );
    } else {
      __asm__  __volatile__( "cli" );
      kprintf( "[Client NA]: Can't receive response from server ! %d\n", r );
      break;
    }
    wait_ticks(TICKS_TO_WAIT, "4");
  }
  sys_exit(0);
  for(;;);
}

static void thread3(void *data)
{
  status_t r;
  char _buf[64];
  char *test_data="Server, are you there [1]?";
  ulong_t snd_len=strlen(test_data)+1;

  kprintf( "Creating client N2 ...\n" );
  if( kernel_thread(thread4,NULL, NULL) != 0 ) {
    panic( "Can't create client thread N2 for testing port IPC functionality !\n" );
  }

  kprintf( "Creating client N3 ...\n" );
  if( kernel_thread(thread5,NULL, NULL) != 0 ) {
    panic( "Can't create client thread N3 for testing port IPC functionality !\n" );
  }

  kprintf( "Creating client N4 ...\n" );
  if( kernel_thread(thread6,NULL,NULL) != 0 ) {
    panic( "Can't create client thread N4 for testing port IPC functionality !\n" );
  }

  sys_exit(0);
  for(;;);

  while(1) {
    memset(_buf,0,sizeof(_buf));
    kprintf( "[Client N1]: Sending data to server via %d...\n",
             server_port3);
    r = ipc_port_send(server_task,server_port3,(ulong_t)test_data,
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

typedef struct __irq_arrray {
  irq_event_mask_t ev_mask;
  irq_counter_t irq_counters[];
} irq_array_t;

static irq_array_t __irq_array __attribute__((aligned(PAGE_SIZE)));

void interrupt_thread_isr(void *data)
{
    ulong_t irqs[]={10,6,1,4};
    static int num_irqs=4;
    status_t id=sys_create_irq_counter_array(irqs,num_irqs,
                                             &__irq_array,0);
    kprintf( "[ISR Thread]: IRQ buffer id: %d\n",id );

    if( id >= 0 ) {
        int i=0;
        status_t r;
        char code;
        int pid=current_task()->pid;

        r= sys_scheduler_control(pid,SYS_SCHED_CTL_SET_PRIORITY,32);
        kprintf( "SCHED_CTRL FOR %d: %d\n",pid,r );
        if( r !=0 ) {
            for(;;);
        }

        code=inb(0x60);
        while( i < 5000000 ) {
            kprintf( "[ISR Thread] Waiting for irqs to arrive ...\n" );
            r=sys_wait_on_irq_array(id);
            if( !r ) {
                ulong_t mask=__irq_array.ev_mask;
                __irq_array.ev_mask=0;
                code=inb(0x60);
                kprintf( "[ISR Thread] CNT: %d, KEY: %d, EVENT MASK: %X\n",
                         __irq_array.irq_counters[2],
                         (int)code,
                         mask);
            }
            i++;
        }
    }

    {
        uint64_t target_tick = swks.system_ticks_64 + 100;

        while(1) {
            if( swks.system_ticks_64 >= target_tick ) {
                kprintf( " + [ISR Thread] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
                         cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
                target_tick += STEP;
            }
        }
    }
}

static void __ticker(void *data)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  kprintf( " + [Ticker] Starting ... Ticks: %d, Target tick: %d\n",
              swks.system_ticks_64, target_tick);
  while(1) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Ticker] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      target_tick += STEP;
    }
   }  
}

void interrupt_thread(void *data) {
   uint64_t target_tick = swks.system_ticks_64 + 100;

   if( kernel_thread(__ticker,NULL,NULL) != 0 ) {
       panic( "Can't create ISR thread for testing interrupt events !\n" );
   }

   kprintf( " + [Interrupt thread] Starting ... Ticks: %d, Target tick: %d\n",
            swks.system_ticks_64, target_tick);

   kprintf( " + [Interrupt thread] Exiting ...\n" );
   sys_exit(0);

   while(1) {
       if( swks.system_ticks_64 >= target_tick ) {
           kprintf( " + [Interrupt thread] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
                    cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
           target_tick += STEP;
       }
   }
}

status_t sys_log(ulong_t s)
{
    kprintf( "LOG: %d\n",s );
    return 0;
}

static void timer_thread(void *data)
{
    ulong_t timeout=390;
    timeval_t tv;

//    kprintf( "[KERNEL THREAD] Sleeping for %d ticks.\n",timeout );
    tv.tv_sec=0;
    tv.tv_nsec=80850000;
//    sleep(timeout);
    sys_nanosleep(&tv,NULL);
    kprintf( "[KERNEL THREAD] Got woken up !\n" );
    for(;;);
}

static void traveller_thread(void *d)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  kprintf( "[TRAVELLER]: Starting on CPU %d\n", cpu_id() );

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [TRAVELLER #%d] Ticks: %d, PID: %d, ATOM: %d\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      target_tick += STEP;
    }
  }
}

static void __migration_thread(void *t)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;
  task_t *traveller;
  int target_cpu=1;
  status_t r;
  bool stopped=false;

  kprintf( "[MIGRATOR]: Starting on CPU %d\n", cpu_id() );
  if( kernel_thread(traveller_thread,NULL,&traveller) ) {
    panic( "Can't create the Traveller !" );
  }

  kprintf( "[MIGRATOR]: Traveller's CPU: %d, PID: %d\n",
           traveller->cpu, traveller->pid);

  r=sched_move_task_to_cpu(traveller,target_cpu);
  if( r ) {
    kprintf( "[MIGRATOR]: Can't move Traveller to CPU %d ! r=%d",
             target_cpu,r);
    for(;;);
  }

  kprintf( " + [MIGRATOR #%d] Ticks: %d, PID: %d, ATOM: %d\n",
           cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [MIGRATOR #%d] Ticks: %d, PID: %d, ATOM: %d\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      if( swks.system_ticks_64 >= 1000 && !stopped ) {
        kprintf( " + [MIGRATOR]: Deactivating the Traveller ... : %d\n",
                 sched_change_task_state(traveller,TASK_STATE_SLEEPING) );
        stopped=true;
      }
      target_tick += STEP;
    }
  }
}

static void ta(void *d)
{
  kprintf( ">>> ACTION !\n" );
}

ipc_gen_port_t *__s_port;
ulong_t __s_port_id;
pid_t server_pid;

extern status_t __sys_port_send(ulong_t channel,ulong_t flags,
                                uintptr_t snd_buf,ulong_t snd_size,
                                uintptr_t rcv_buf,ulong_t rcv_size);

status_t __sys_port_reply(ulong_t port,ulong_t msg_id,ulong_t reply_buf,
                          ulong_t reply_len);

static void __new_port_logic_client_thread(void *t)
{
  ipc_port_message_t *msg1,*msg2,*msg3;
  char *data1="Hello, server 1!";
  char *data2="Hello, server 2!";
  char *data3="Hello, server 3!";
  status_t r,ch_id,i;
  char buf[64];

  kprintf( "[NEW PORT CLIENT THREAD]: Starting. Creating channels to %d:%d\n",
           server_pid,__s_port_id);

  for(i=0;i<10;i++) {
    ch_id=sys_open_channel(server_pid,__s_port_id);
    if( ch_id < 0 ) {
      kprintf( "[NEW PORT CLIENT THREAD]: Failed to create channel N%d: %d\n",
               i,ch_id);
      for(;;);
    }
    if( ch_id != i ) {
      kprintf( "[NEW PORT CLIENT THREAD]: Channel ID mismatch ! %d:%d\n",
               ch_id,i);
      for(;;);
    }
  }
  kprintf( "[NEW PORT CLIENT THREAD]: Channels created: %d\n",i );

  r=sys_close_channel(2);
  kprintf( " [NEW PORT CLIENT THREAD]: Closing channel N2: %d\n",
           r);
  if( !r ) {
    r=__sys_port_send(2,IPC_BLOCKED_ACCESS,data1,strlen(data1)+1,buf,
                      sizeof(buf));
    kprintf( "[NEW PORT CLIENT THREAD]: Trying to send a message over a closed channel: %d\n",
             r);
  }

  kprintf( "[NEW PORT CLIENT THREAD]: Done !\n" );
  for(;;);
/*
  msg1=__ipc_create_nb_port_message(current_task(),data1,
                                   strlen(data1)+1);
  msg2=__ipc_create_nb_port_message(current_task(),data2,
                                    strlen(data2)+1);
  msg3=__ipc_create_nb_port_message(current_task(),data3,
                                   strlen(data3)+1);
  if( msg1 != NULL && msg2 != NULL && msg3 != NULL ) {
    kprintf( "[NEW PORT CLIENT THREAD]: Sending 3 messages to the Server.\n" );
    r=__ipc_port_send(__s_port,msg1,0,0,0);
    r |=__ipc_port_send(__s_port,msg2,0,0,0);
    r |=__ipc_port_send(__s_port,msg3,0,0,0);
    kprintf( "[NEW PORT CLIENT THREAD]: Message send statistics: %d\n",r );
    }
*/

  for(i=0;i<2;i++) {
    kprintf( "[NEW PORT CLIENT THREAD]: Sending messages to the Server.\n" );
    r=__sys_port_send(0,IPC_BLOCKED_ACCESS,data1,strlen(data1)+1,buf,
                      sizeof(buf));
    kprintf( "  r=%d\n",r );
    if( r>=0 ) {
      kprintf( "[NEW PORT CLIENT THREAD]: Reply data: %s\n",
               buf );
    }
/*
    r =__sys_port_send(1,0,data2,strlen(data2)+1,0,0);
    kprintf( "  r=%d\n",r );
    r =__sys_port_send(3,0,data3,strlen(data3)+1,0,0);
    kprintf( "  r=%d\n",r );
    kprintf( "[NEW PORT CLIENT THREAD]: Message send statistics: %d\n",r );
*/
    __wait_ticks(1000, "[NEW PORT CLIENT]" );
  }

  kprintf( "[NEW PORT CLIENT THREAD]: Done !\n" );
  for(;;);
}

static void __new_port_logic_thread(void *t)
{
  status_t r;
  int i;
  char buf[64];
  port_msg_info_t msg_info;
  char *reply_data="Hello from server !";

  kprintf( "[NEW PORT THREAD]: Starting ...\n" );
  server_pid=current_task()->pid;

  for(i=0;i<6;i++) {
    kprintf( "[NEW PORT THREAD]: Creating a port: " );
    r=__ipc_create_port(current_task(),IPC_BLOCKED_ACCESS);
    kprintf( "%d\n", r );
  }

  __s_port_id=4;
  r=__ipc_get_port(current_task(),__s_port_id,&__s_port);
  if( !r ) {
    if( kernel_thread( __new_port_logic_client_thread,NULL,NULL) ) {
      panic( "Can't create Migration thread !" );
    }

    while( 1 ) {
      kprintf( "[NEW PORT THREAD]: Listening on port %d ...\n", __s_port_id );
      r=__ipc_port_receive(__s_port,IPC_BLOCKED_ACCESS,buf,sizeof(buf),
                           &msg_info);
      if( !r ) {
        kprintf( "[NEW PORT THREAD]: Msg ID: %d, Data size: %d\n",
                 msg_info.msg_id,msg_info.msg_len);
        kprintf( "Data: %s\n", buf );
        r=__sys_port_reply(__s_port_id,msg_info.msg_id,reply_data,
                           strlen(reply_data)+1);
        kprintf( "[NEW PORT THREAD]: Reply status: %d\n", r );
      } else {
        kprintf( "[NEW PORT THREAD]: Message receive failed ! %d\n", r );
        break;
      }
    }
  } else {
    kprintf( "[NEW PORT THREAD]: Can't resolve port %d ! r=%d\n",
             __s_port_id,r );
  }
  kprintf( "[NEW PORT THREAD]: Done !\n" );
  for(;;);
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;
  bool flag=false;

/*
  if( !cpu_id() ) {
    spawn_percpu_threads();
  }
*/

  if( !cpu_id() ) {
    if( kernel_thread( __new_port_logic_thread,NULL,NULL) ) {
      panic( "Can't create Migration thread !" );
    }
  }

/*
  if( !cpu_id() ) {
    if( kernel_thread( __migration_thread,NULL,NULL) ) {
      panic( "Can't create Migration thread !" );
    }
  }
*/
  /*
  if( cpu_id() == 0 ) {
      if( kernel_thread(timer_thread,NULL,NULL) != 0 ) {
          panic( "Can't create server thread for testing port IPC functionality !\n" );
      }
      }
  */
  /*
  if( cpu_id() == 0 ) {
      if( kernel_thread(interrupt_thread,NULL,NULL) != 0 ) {
          panic( "Can't create server thread for testing interrupt events !\n" );
      }
  }
  */
/*
  if( cpu_id() == 0 ) {
    if( kernel_thread(thread2,NULL,NULL) != 0 ) {
      panic( "Can't create server thread for testing port IPC functionality !\n" );
    }
    if( kernel_thread(thread3,NULL,NULL) != 0 ) {
      panic( "Can't create client thread for testing port IPC functionality !\n" );
    }
  }
*/
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

    /*
    if( cpu_id() && swks.system_ticks_64 > 400 && !flag ) {
      gc_action_t *a=gc_allocate_action(ta,NULL);

      kprintf( "++ Activating action ...\n" );
      gc_schedule_action(a);
      flag=true;
      }*/
  }
}
