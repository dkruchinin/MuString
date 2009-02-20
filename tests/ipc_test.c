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
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/process.h>
#include <ds/linked_array.h>
#include <eza/usercopy.h>
#include <config.h>

#define TEST_ID  "IPC subsystem test"
#define SERVER_THREAD  "[SERVER THREAD] "
#define CLIENT_THREAD  "[CLIENT THREAD] "

#define DECLARE_TEST_CONTEXT  ipc_test_ctx_t *tctx=(ipc_test_ctx_t*)ctx; \
  test_framework_t *tf=tctx->tf

#define SERVER_NUM_PORTS  10
#define NON_BLOCKED_PORT_ID (SERVER_NUM_PORTS-1)
#define BIG_MESSAGE_PORT_ID 5

#define TEST_ROUNDS  3
#define SERVER_NUM_BLOCKED_PORTS  NON_BLOCKED_PORT_ID

typedef struct __ipc_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
} ipc_test_ctx_t;

typedef struct __thread_port {
  ulong_t port_id,server_pid;
  ipc_test_ctx_t *tctx;
  bool finished_tests;
  long msg_id;
  long reply_msg_id;
  int pid;
} thread_port_t;

#define MAX_TEST_MESSAGE_SIZE 512
static char __c_rcv_buf[CONFIG_NRCPUS][MAX_TEST_MESSAGE_SIZE];

#define __client_rcv_buf (char *)&__c_rcv_buf[cpu_id()][0]

static char __server_rcv_buf[MAX_TEST_MESSAGE_SIZE];

#define BIG_MESSAGE_SIZE  (1500*1024)
static uint8_t __big_message_pattern[BIG_MESSAGE_SIZE+sizeof(int)];
static uint8_t __big_message_server_buf[BIG_MESSAGE_SIZE+sizeof(int)];
static uint8_t __big_message_client_buf[BIG_MESSAGE_SIZE+sizeof(int)];

static char *patterns[TEST_ROUNDS]= {
  "1",
  "1111111111111111111111111111112222222222222222222222222222222222222222222222222",
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555"
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555"
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555",
};

#define BIG_MSG_ID  TEST_ROUNDS
static SPINLOCK_DEFINE(__shared_data_lock);

#define LOCK_SHARED_DATA()   spinlock_lock(&__shared_data_lock)
#define UNLOCK_SHARED_DATA() spinlock_unlock(&__shared_data_lock)

static bool __verify_message(ulong_t id,char *msg)
{
  return !memcmp(patterns[id],msg,strlen(patterns[id])+1);
}

static bool __verify_big_message(uint8_t *buf,ulong_t *diff_offset)
{
  int i;
  uint8_t *p=__big_message_pattern;

  for(i=0;i<BIG_MESSAGE_SIZE;i++) {
    if( *p != *buf ) {
      *diff_offset=i;
      return false;
    }
    p++;
    buf++;
  }
  return true;
}

static ulong_t __v_2[]={10,57};
static ulong_t __v_6[]={8,8,4,10,10,30};
static ulong_t __v_4[]={2,10,10,39};

static ulong_t __v_big_7[]={1156,32500,79000,217299,
                            376415,121783,573200};

void __setup_iovectors(ulong_t msg_id,iovec_t *iovecs,ulong_t *numvecs)
{
  uint8_t *p;
  ulong_t len;
  ulong_t *lengths,chunks;
  int i;

  if( msg_id != BIG_MSG_ID ) {
    p=(uint8_t *)patterns[msg_id];
    len=strlen(patterns[msg_id])+1;
  }

  if( msg_id == 0 ) {
    iovecs->iov_base=p;
    iovecs->iov_len=len;
    *numvecs=1;
    return;
  } else if( msg_id == BIG_MSG_ID ) {
    p=__big_message_pattern;
    len=BIG_MESSAGE_SIZE;
    lengths=__v_big_7;
    chunks=7;
  } else {

    switch( current_task()->pid % 3 ) {
      case 0:
        lengths=__v_2;
        chunks=2;
        break;
      case 1:
        lengths=__v_6;
        chunks=6;
        break;
      case 2:
        lengths=__v_4;
        chunks=4;
        break;
    }
  }

  for(i=0;i<chunks;i++) {
    iovecs->iov_base=p;
    iovecs->iov_len=lengths[i];

    iovecs++;
    len-=lengths[i];
    p+=lengths[i];
  }
  /* Process the last chunk. */
  iovecs->iov_base=p;
  iovecs->iov_len=len;
  *numvecs=chunks+1;
}

static void __client_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int i;
  int channels[SERVER_NUM_PORTS];
  int r;
  iovec_t iovecs[MAX_IOVECS];
  ulong_t numvecs;
  
  tf->printf(CLIENT_THREAD "Opening %d channels.\n",SERVER_NUM_PORTS );
  for(i=0;i<SERVER_NUM_PORTS;i++) {
    ulong_t flags;

    if( i != NON_BLOCKED_PORT_ID ) {
      flags |= IPC_CHANNEL_FLAG_BLOCKED_MODE;
    } else {
      flags = 0;
    }

    channels[i]=sys_open_channel(tctx->server_pid,i,
                                 flags);
    if( channels[i] != i ) {
      tf->printf(CLIENT_THREAD "Channel number mismatch: %d instead of %d\n",
                 channels[i],i);
      tf->failed();
      goto exit_test;
    }

    /* Now check if channel's flags macth. */
    r=sys_control_channel(channels[i],IPC_CHANNEL_CTL_GET_SYNC_MODE,0);
    if( r >= 0) {
      int shouldbe=(i != NON_BLOCKED_PORT_ID) ? 1 : 0;
      if( r != shouldbe ) {
        tf->printf(CLIENT_THREAD "Synchronous mode flag mismatch for channel %d ! %d:%d\n",
                   channels[i],r,shouldbe);
        tf->failed();
      }
    } else {
      tf->printf(CLIENT_THREAD"Can't read channel's sync mode: %d\n",r );
    }
  }
  tf->passed();

  tf->printf(CLIENT_THREAD "Trying to open a channel to insufficient port number.\n" );
  r=sys_open_channel(tctx->server_pid,SERVER_NUM_PORTS,
                     IPC_CHANNEL_FLAG_BLOCKED_MODE);
  if( r == -EINVAL ) {
    tf->passed();
  } else {
    tf->failed();
  }

  /****************************************************************
   * Sending messages in blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Sending messages in blocking mode.\n" );  
  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_send(channels[i],
                    (ulong_t)patterns[i],strlen(patterns[i])+1,
                    (ulong_t)__client_rcv_buf,MAX_TEST_MESSAGE_SIZE);
    if( r < 0 ) {
      tf->printf(CLIENT_THREAD "Error while sending data over channel N%d : %d\n",
                 channels[i],r);
      tf->failed();
      goto exit_test;
    } else {
        if( r == strlen(patterns[i])+1 ) {
          if( !__verify_message(i,__client_rcv_buf) ) {
            tf->printf(CLIENT_THREAD "Reply to message N%d mismatch.\n",
                       i);
            tf->failed();
          }
        } else {
          tf->printf(CLIENT_THREAD "Reply to message N%d has insufficient length: %d instead of %d\n",
                     i,r,strlen(patterns[i])+1);
          tf->failed();
        }
    }
  }
  tf->printf(CLIENT_THREAD "All messages were successfully processed.\n" );
  tf->passed();

  /****************************************************************
   * Sending a big message in blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Sending a big message in blocking mode via I/O vectors.\n" );
  __setup_iovectors(BIG_MSG_ID,iovecs,&numvecs);

  r=sys_port_send_iov(channels[BIG_MESSAGE_PORT_ID],
                      iovecs,numvecs,
                      (ulong_t)__big_message_client_buf,BIG_MESSAGE_SIZE);
  if( r < 0 ) {
    tf->printf(CLIENT_THREAD "Error while sending a big message over channel N%d : %d\n",
               channels[BIG_MESSAGE_PORT_ID],r);
    tf->failed();
    goto exit_test;
  } else {
    kprintf( CLIENT_THREAD "Got big reply: %d bytes.\n",r );
    if( r == BIG_MESSAGE_SIZE ) {
      ulong_t offt;

      if( !__verify_big_message(__big_message_client_buf,&offt) ) {
        tf->printf(CLIENT_THREAD "Server reply mismatch at offset: %d\n",
                   offt);
        tf->failed();
      } else {
        tf->printf(CLIENT_THREAD "Big message was successfully replied.\n" );
        tf->passed();
      }
    } else {
      tf->printf(CLIENT_THREAD "Server reply has insufficient length: %d instead of %d\n",
                 r,BIG_MESSAGE_SIZE);
      tf->failed();
    }
  }

  /****************************************************************
   * Sending messages in non-blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Sending messages in non-blocking mode (using channel N%d).\n",
             channels[NON_BLOCKED_PORT_ID]);
  for( i=0; i<TEST_ROUNDS;i++ ) {
    r=sys_port_send(channels[NON_BLOCKED_PORT_ID],
                    (ulong_t)patterns[i],strlen(patterns[i])+1,
                    0,0);
    if( r < 0 ) {
      tf->printf(CLIENT_THREAD "Error while sending data over channel N%d : %d\n",
                 channels[NON_BLOCKED_PORT_ID],r);
      tf->failed();
      goto exit_test;
    }
  }
  tf->printf(CLIENT_THREAD "All messages were successfully sent.\n" );
  tf->passed();

  goto exit_test;
exit_test:
  sys_exit(0);
}

#define POLL_CLIENT "[POLL CLIENT] "
static void __poll_client(void *d)
{
  thread_port_t *tp=(thread_port_t *)d;
  ipc_test_ctx_t *tctx=tp->tctx;
  test_framework_t *tf=tctx->tf;
  ulong_t channel,port;
  int i,r;
  ulong_t msg_id;
  char client_rcv_buf[MAX_TEST_MESSAGE_SIZE];
  iovec_t iovecs[MAX_IOVECS];

  port=tp->port_id;
  r=sys_open_channel(tp->server_pid,port,IPC_CHANNEL_FLAG_BLOCKED_MODE);
  if( r < 0 ) {
    tf->printf(POLL_CLIENT "Can't open a channel to %d:%d ! r=%d\n",
               tp->server_pid,port,r);
    tf->abort();
  }
  channel=r;

  msg_id=port % TEST_ROUNDS;
  for(i=0;i<TEST_ROUNDS;i++) {
    char *snd_type;
    tf->printf(POLL_CLIENT "Sending message to port %d.\n",port );

    if( i & 0x1 ) {    
    r=sys_port_send(channel,
                    (ulong_t)patterns[msg_id],strlen(patterns[msg_id])+1,
                    (ulong_t)client_rcv_buf,sizeof(client_rcv_buf));
    snd_type="sys_port_send()";
    } else {
      /* Sending a message using I/O vector array. */
      ulong_t numvecs;

      snd_type="sys_port_send_iov()";
      __setup_iovectors(msg_id,iovecs,&numvecs);
      r=sys_port_send_iov(channel,iovecs,numvecs,
                    (ulong_t)client_rcv_buf,sizeof(client_rcv_buf));
    }
    if( r < 0 ) {
      tf->printf(POLL_CLIENT "Error occured while sending message: %d via %s\n",
                 r,snd_type);
      tf->failed();
    } else {
      if( r == strlen(patterns[msg_id])+1 ) {
        if( !__verify_message(msg_id,client_rcv_buf) ) {
          tf->printf(POLL_CLIENT "Reply to my message mismatches !\n");
          tf->failed();
        }
      } else {
        tf->printf(POLL_CLIENT "Reply to my message has insufficient length: %d instead of %d\n",
                   r,strlen(patterns[msg_id])+1);
        tf->failed();
      }

      tf->printf(POLL_CLIENT "Message from port %d was successfully replied.\n",
                 port);
      sleep(HZ/50);
    }
  }

  /* Special case: test client wake up during port close. */
  if( port == 0 ) {
    tf->printf(POLL_CLIENT "Testing correct client wake-up on port closing...\n" );
    r=sys_port_send(channel,
                    (ulong_t)patterns[msg_id],strlen(patterns[msg_id])+1,
                    (ulong_t)client_rcv_buf,sizeof(client_rcv_buf));
    if( r == -EPIPE ) {
      tf->printf( POLL_CLIENT "Got -EPIPE. It seems that kernel woke us properly.\n" );
      tp->finished_tests=true;
    } else {
      tf->printf(POLL_CLIENT "Insufficient return value upon wake-up on port closing: %d\n",
                 r);
      tf->failed();
    }
  } else {
    tp->finished_tests=true;
  }

  tf->printf(POLL_CLIENT "Exiting (target port: %d)\n",port );
  sys_exit(0);
}

#define NUM_POLL_CLIENTS  SERVER_NUM_BLOCKED_PORTS

thread_port_t poller_ports[NUM_POLL_CLIENTS];

static void __ipc_poll_test(ipc_test_ctx_t *tctx,int *ports)
{
  int i,r,j;
  pollfd_t fds[NUM_POLL_CLIENTS];
  test_framework_t *tf=tctx->tf;
  ulong_t polled_clients;
  port_msg_info_t msg_info;
  char server_rcv_buf[MAX_TEST_MESSAGE_SIZE];

  for(i=0;i<NUM_POLL_CLIENTS;i++) {
    task_t *task;

    memset(&poller_ports[i],0,sizeof(poller_ports[i]));

    poller_ports[i].port_id=ports[i];
    poller_ports[i].tctx=tctx;
    poller_ports[i].server_pid=current_task()->pid;
    poller_ports[i].finished_tests=false;
    poller_ports[i].msg_id=-1;

    if( kernel_thread(__poll_client,&poller_ports[i],&task ) ) {
      tf->printf( "Can't create a client thread for testing IPC poll !\n" );
      tf->abort();
    }
    poller_ports[i].pid=task->pid;
  }

  tf->printf(SERVER_THREAD "Client threads created. Ready for polling ports.\n");
  for(j=0;j<TEST_ROUNDS;j++) {
    ulong_t msg_id;
    thread_port_t *_tt;

    polled_clients=NUM_POLL_CLIENTS;

    while(polled_clients) {
      for(i=0;i<NUM_POLL_CLIENTS;i++) {
        fds[i].events=POLLIN | POLLRDNORM;
        fds[i].revents=0;
        fds[i].fd=ports[i];
      }

      tf->printf( SERVER_THREAD "Polling ports (%d clients left) ...\n",polled_clients );
      r=sys_ipc_port_poll(fds,NUM_POLL_CLIENTS,NULL);
      if( r < 0 ) {
        tf->printf( SERVER_THREAD "Error occured while polling ports: %d\n",r );
        tf->abort();
      }
      int jl,nc=r;

      tf->printf( SERVER_THREAD ">> Events occured: %d\n", r );
      polled_clients-=r;

      for(jl=0;jl<NUM_POLL_CLIENTS;jl++) {
        if( fds[jl].revents ) {
          r=sys_port_receive(fds[jl].fd,0,(ulong_t)server_rcv_buf,
                             sizeof(server_rcv_buf),&msg_info);

          if( r != 0 ) {
            tf->printf( SERVER_THREAD "Error during RCV from port N %d. r=%d\n",
                        fds[jl].fd,r);
            tf->abort();
          }

          if( !nc ) {
            tf->printf(SERVER_THREAD"Extra poll events available !\n");
            tf->abort();
          }
          nc--;

          msg_id=fds[jl].fd % TEST_ROUNDS;
          if( msg_info.msg_len == strlen(patterns[msg_id])+1 ) {
            if( !__verify_message(msg_id,server_rcv_buf) ) {
              tf->printf( SERVER_THREAD "Poll: Message N %d mismatch.\n",
                          fds[i].fd );
              tf->abort();
            }
          } else {
            tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                       fds[i].fd,msg_info.msg_len,strlen(patterns[msg_id])+1);
            tf->abort();
          }

          /* Good message, so store its details for further reply. */
          _tt=NULL;
          for(i=0;i<NUM_POLL_CLIENTS;i++) {
            if( poller_ports[i].pid == msg_info.sender_pid ) {
              _tt=&poller_ports[i];
            }
          }

          if( !_tt ) {
            tf->printf(SERVER_THREAD"Can't locate poll client: %d\n",
                       msg_info.sender_pid);
            tf->abort();
          }

          if( fds[jl].fd != _tt->port_id ) {
            tf->printf(SERVER_THREAD"Insufficient poll client's (%d) port: %d instead of %d !\n",
                       jl,fds[jl].fd,_tt->port_id);
            tf->abort();
          }

          if( _tt->msg_id != -1 ) {
            tf->printf(SERVER_THREAD"Another message for client %d ! (%d)\n",
                       msg_info.sender_pid,_tt->msg_id);
            tf->abort();
          }
          _tt->msg_id=msg_info.msg_id;
          _tt->reply_msg_id=msg_id;
        }
      }

      if( nc ) {
        tf->printf(SERVER_THREAD"%d not processed events after current poll round !\n",nc);
        tf->abort();
      }
    }
    tf->printf( SERVER_THREAD "All clients sent their messages. Replying to them ...\n" );

    for(i=0;i<NUM_POLL_CLIENTS;i++) {
      _tt=&poller_ports[i];

      if( _tt->msg_id == -1 ) {
        tf->printf(SERVER_THREAD"Not processed poll client found! %d\n",
                   _tt->pid);
        tf->abort();
      }
      r=sys_port_reply(_tt->port_id,_tt->msg_id,
                       (ulong_t)patterns[_tt->reply_msg_id],
                       strlen(patterns[_tt->reply_msg_id])+1);
      if( r ) {
        tf->printf(SERVER_THREAD "Error occured during replying message %d via port %d. r=%d\n",
                   _tt->msg_id,_tt->port_id,r);
        tf->abort();
      }
      _tt->msg_id=-1;
    }
    tf->printf( SERVER_THREAD "All messages were replied. Now let's have some rest (HZ/2) ...\n" );
    sleep(HZ/2);
  }

  /*****************************************************************
   * Testing client wake-up on port closing.
   ****************************************************************/
  /* By now poll client that communicates with port N0 is still running
   * and waiting for us to reply it.
   * But we will just close the port to check if it is still sleeping,
   * or not.
   */
  tf->printf(SERVER_THREAD "Testing client wake-up during port closing.\n" );
  r=sys_port_receive(0,0,(ulong_t)server_rcv_buf,
                     sizeof(server_rcv_buf),&msg_info);
  if( r != 0 ) {
    tf->printf( SERVER_THREAD "Error during receiving message for client wake-up. r=%d\n",
                r);
    tf->abort();
  }

  /* Simulate an accident port shutdown. */
  r=sys_close_port(0);
  if( r ) {
    tf->printf( SERVER_THREAD "Can't close port N 0. r=%d\n",r );
    tf->abort();
  }

  /* Let sleep for a while to allow the client to let us know about its state. */
  sleep(HZ/2);
  if( poller_ports[0].finished_tests ) {
    tf->printf( SERVER_THREAD "Clien't was successfully woken up.\n" );
    tf->passed();
  } else {
    tf->printf( SERVER_THREAD "Client wasn't woken up !\n",r );
    tf->failed();
  }
  tf->printf( SERVER_THREAD "All poll tests finished.\n" );
}

static void __notifier_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  uint64_t target_tick=system_ticks + 200;

  tf->printf( "[Notifier] Starting.\n" );

  while(system_ticks < target_tick) {
  }

  tf->printf( "[Notifier] Exiting.\n" );
  sys_exit(0);
}

static void __process_events_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int port=sys_create_port(0,0);
  task_t *task;
  int r;
  task_event_ctl_arg te_ctl;
  task_event_descr_t ev_descr;
  port_msg_info_t msg_info;

  if( port < 0 ) {
    tf->printf( "Can't create a port !\n" );
  }

  if( kernel_thread(__notifier_thread,ctx,&task) ) {
    tf->printf("Can't create the Notifier !\n");
    tf->abort();
  }

  te_ctl.ev_mask=TASK_EVENT_TERMINATION;
  te_ctl.port=port;
  r=sys_task_control(task->pid,SYS_PR_CTL_ADD_EVENT_LISTENER,
                     (ulong_t)&te_ctl);
  if( r ) {
    tf->printf("Can't set event listener: %d\n",r);
    tf->failed();
  }

  tf->printf( "Check that no one can set more than one same listeners.\n" );
  te_ctl.ev_mask=TASK_EVENT_TERMINATION;
  te_ctl.port=port;
  r=sys_task_control(task->pid,SYS_PR_CTL_ADD_EVENT_LISTENER,
                     (ulong_t)&te_ctl);
  if( r != -EBUSY ) {
    tf->printf("How did I manage to set the second listener ? %d\n",r);
    tf->failed();
  } else {
    tf->passed();
  }

  memset(&ev_descr,0,sizeof(ev_descr));
  r=sys_port_receive(port,IPC_BLOCKED_ACCESS,(ulong_t)&ev_descr,
                     sizeof(ev_descr),&msg_info);
  if( r ) {
    tf->printf("Error occured while waiting for task's events: %d !\n",
               r);
    tf->failed();
  }

  if( ev_descr.pid != task->pid || ev_descr.tid != task->tid ||
      !(ev_descr.ev_mask & te_ctl.ev_mask) ) {
    tf->printf( "Improper notification message received: PID: %d, TID: %d, EVENT: 0x%X\n",
                ev_descr.pid,ev_descr.tid,ev_descr.ev_mask);
    tf->failed();
  }

  tf->printf( "All process event tests passed.\n" );
  tf->passed();

  sys_close_port(port);
}

#define VECTORER_ID "[VECTORER] "

#define MSG_HEADER_DATA_SIZE  715      //  715
#define MSG_PART_DATA_SIZE    3419     // 3419
#define MSG_TAIL_DATA_SIZE    17918    //17918

typedef struct __message_header {
  uint16_t data_base;
  uint16_t data[MSG_HEADER_DATA_SIZE];
} message_header_t;

typedef struct __message_part {
  uint16_t data_base;
  uint16_t data[MSG_PART_DATA_SIZE];
} message_part_t;

typedef struct __message_tail {
  uint16_t data_base;
  uint16_t data[MSG_TAIL_DATA_SIZE];  
} message_tail_t;

#define FAIL_ON(c,tf)  do {                     \
    if( (c) ) (tf)->failed();                   \
  } while(0)

#define MAX_MSG_PARTS  6
#define MAX_MSG_SIZE (sizeof(message_header_t) + MAX_MSG_PARTS*sizeof(message_part_t) + sizeof(message_tail_t))

static uint8_t __vmsg_client_snd_buf[CONFIG_NRCPUS][MAX_MSG_SIZE];
static uint8_t __vmsg_client_rcv_buf[CONFIG_NRCPUS][MAX_MSG_SIZE];
static uint8_t __vectored_msg_server_snd_buf[MAX_MSG_SIZE];
static uint8_t __vectored_msg_server_rcv_buf[MAX_MSG_SIZE];

#define __vectored_msg_client_snd_buf  (uint8_t*)&__vmsg_client_snd_buf[cpu_id()][0]
#define __vectored_msg_client_rcv_buf  (uint8_t*)&__vmsg_client_rcv_buf[cpu_id()][0]

static int __validate_message_data(uint16_t *data,uint16_t base,ulong_t size)
{
  int i;

  for(i=0;i<size;i++,data++,base++) {
    if( *data != base ) {
      kprintf( "M: %d instead of %d at %d\n", *data,base,i );
      return i ? -i : -100000000;
    }
  }
  return 0;
}

static void __prepare_message_data(uint16_t *data,uint16_t base,ulong_t size)
{
  while( size ) {
    *data=base;
    size--;
    data++;
    base++;
  }
}

static bool __validate_vectored_message(uint8_t *msg,int parts,
                                        test_framework_t *tf)
{
  message_header_t *hdr=(message_header_t*)msg;
  message_part_t *part;
  message_tail_t *tail;
  int r,i;

  r=__validate_message_data(hdr->data,hdr->data_base,MSG_HEADER_DATA_SIZE);
  if( r < 0 ) {
    tf->printf("Message header mismatches at %d\n",-r);
    return false;
  }

  hdr++;
  part=(message_part_t *)hdr;
  for(i=0;i<parts;i++,part++) {
    r=__validate_message_data(part->data,part->data_base,MSG_PART_DATA_SIZE);
    if( r < 0 ) {
      tf->printf("Message part %d mismatches at %d\n",i,-r);
      return false;
    }
  }

  tail=(message_tail_t *)part;

  r=__validate_message_data(tail->data,tail->data_base,MSG_TAIL_DATA_SIZE);
  if( r < 0 ) {
    tf->printf("Message tail mismatches at %d\n",-r);
    return false;
  }
  return true;
}

static uint16_t __data_base=210;
#define DATA_BASE_STEP  75

static void __prepare_vectored_message(uint8_t *buf,int parts)
{
  message_header_t *hdr=(message_header_t*)buf;
  message_part_t *part;
  message_tail_t *tail;
  int i;

  LOCK_SHARED_DATA();
  hdr->data_base=__data_base;
  __data_base += DATA_BASE_STEP;
  UNLOCK_SHARED_DATA();
  __prepare_message_data(hdr->data,hdr->data_base,MSG_HEADER_DATA_SIZE);

  hdr++;
  part=(message_part_t *)hdr;
  for(i=0;i<parts;i++,part++) {
    LOCK_SHARED_DATA();
    part->data_base=__data_base;
    __data_base += DATA_BASE_STEP;
    UNLOCK_SHARED_DATA();
    __prepare_message_data(part->data,part->data_base,MSG_PART_DATA_SIZE);
  }

  tail=(message_tail_t *)part;
    LOCK_SHARED_DATA();
    tail->data_base=__data_base;
    __data_base += DATA_BASE_STEP;
    UNLOCK_SHARED_DATA();
  __prepare_message_data(tail->data,tail->data_base,MSG_TAIL_DATA_SIZE);
}

static void __setup_message_iovecs(uint8_t *msg,int parts,iovec_t *iovecs)
{
  iovecs->iov_base=msg;
  iovecs->iov_len=sizeof(message_header_t);

  iovecs++;
  msg += sizeof(message_header_t);

  for(;parts;parts--) {
    iovecs->iov_base=msg;
    iovecs->iov_len=sizeof(message_part_t);

    iovecs++;
    msg += sizeof(message_part_t);
  }

  iovecs->iov_base=msg;
  iovecs->iov_len=sizeof(message_tail_t);
}

#define CLEAR_CLIENT_BUFFERS                    \
  memset(__vectored_msg_client_snd_buf,0,MAX_MSG_SIZE); \
  memset(__vectored_msg_client_rcv_buf,0,MAX_MSG_SIZE)

#define CLEAR_SERVER_BUFFERS                                            \
  memset(__vectored_msg_server_snd_buf,0,sizeof(__vectored_msg_server_snd_buf)); \
  memset(__vectored_msg_server_rcv_buf,0,sizeof(__vectored_msg_server_rcv_buf))

static ulong_t __server_pid,__vectored_port;

#define MESSAGE_SIZE(n) (sizeof(message_header_t)+((n)*sizeof(message_part_t))+sizeof(message_tail_t))
#define WL_PATTERN  0x00AA11BB22CC33DD

static void __vectored_messages_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int r,i;
  iovec_t snd_iovecs[MAX_IOVECS],rcv_iovecs[MAX_IOVECS];
  ulong_t channel;
  ulong_t size;

  tf->printf(VECTORER_ID "Starting on CPU %d.\n",cpu_id());

  channel=sys_open_channel(__server_pid,__vectored_port,IPC_BLOCKED_ACCESS);
  if( channel < 0 ) {
    tf->printf(VECTORER_ID"Can't open a channel !\n" );
    tf->abort();
  }

  for(i=0;i<5*TEST_ROUNDS;i++) {
    ulong_t parts=(i%6)+1;
    ulong_t *watchline;

    CLEAR_CLIENT_BUFFERS;
    __prepare_vectored_message(__vectored_msg_client_snd_buf,parts);
    __setup_message_iovecs(__vectored_msg_client_snd_buf,parts,snd_iovecs);

    /* Setup memory watchline. */
    size=MESSAGE_SIZE(parts);
    watchline=(ulong_t*)(__vectored_msg_client_rcv_buf+size);
    *watchline=WL_PATTERN;

    tf->printf(VECTORER_ID "Sending a message consisting of %d parts via %s. SIZE=%d\n",
               parts, (i & 0x1) ? "'sys_port_send_iov()'" : "'sys_port_send_iov_v()'",
               MESSAGE_SIZE(parts));
    if( i & 0x1 ) {
      r=sys_port_send_iov(channel,snd_iovecs,parts+2,
                          (uintptr_t)__vectored_msg_client_rcv_buf,
                          MAX_MSG_SIZE);
      tf->printf(VECTORER_ID"Message was sent: r=%d. RCV BUFSIZE=%d\n",r,
                 sizeof(__vectored_msg_client_rcv_buf));
      if( r < 0 ) {
        tf->failed();
      }
    } else {
      __setup_message_iovecs(__vectored_msg_client_rcv_buf,parts,rcv_iovecs);
      r=sys_port_send_iov_v(channel,snd_iovecs,parts+2,
                            rcv_iovecs,parts+2);
      tf->printf(VECTORER_ID"Message was sent: r=%d. RCV BUFSIZE=%d\n",r,
                 sizeof(__vectored_msg_client_rcv_buf));
      if( r < 0 ) {
        tf->failed();
      }
    }
    tf->printf(VECTORER_ID"Varifying server's reply (%d-parts message).\n",
               parts);
    FAIL_ON(!__validate_vectored_message(__vectored_msg_client_rcv_buf,parts,tf),tf);
    if( *watchline != WL_PATTERN ) {
      tf->printf(VECTORER_ID"Watchline pattern mismatch ! 0x%X instead of 0x%X\n",
                 *watchline,WL_PATTERN);
      tf->failed();
    }
  }

  tf->printf(VECTORER_ID "All messages were successfully transmitted.\n");
  sys_exit(0);
}

static void __validate_retval(int r,int expected,
                              test_framework_t *tf)
{
  if( r != expected ) {
    tf->printf(SERVER_THREAD "Insufficient return value: %d\n",
               r);
    tf->failed();
  }
}

#define READ_LONGS  15500
static uintptr_t __buf_pages[512];
static long __bufzone[32];
static long __zbuffer_snd[READ_LONGS];
static long __zbuffer_rcv[READ_LONGS];
 
#define MIDDLE_PARTS(t)  ((t)-2)

#define NUM_OFFSETS 20

static void __ipc_buffer_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  iovec_t snd_iovecs[MAX_IOVECS],rcv_iovecs[MAX_IOVECS];
  ipc_user_buffer_t bufs[MAX_IOVECS];
  ulong_t __parts;
  unsigned char *p;
  int i,r,__offset;
  long *lp;
  int __offsets[NUM_OFFSETS]={315,10,715,1129,2390,3578,4096,4912,
                              5791,6379,7490,8111,8757,9300,10319,10999,
                              10999,11715,12514,13845};

  tf->printf(SERVER_THREAD"Testing IPC buffers functionality.\n");

  /* 1-part buffer, 3-parts message. */
  tf->printf(SERVER_THREAD"Transferring 6 pieces of data to a 1-part IPC buffer.\n");
  CLEAR_SERVER_BUFFERS;
  __prepare_vectored_message(__vectored_msg_server_snd_buf,4);

  rcv_iovecs[0].iov_base=__vectored_msg_server_rcv_buf;
  rcv_iovecs[0].iov_len=4*sizeof(message_part_t)+sizeof(message_tail_t)+sizeof(message_header_t);

  snd_iovecs[0].iov_base=__vectored_msg_server_snd_buf;
  snd_iovecs[0].iov_len=sizeof(message_header_t);
  snd_iovecs[1].iov_base=snd_iovecs[0].iov_base+snd_iovecs[0].iov_len;
  snd_iovecs[1].iov_len=4*sizeof(message_part_t);
  snd_iovecs[2].iov_base=snd_iovecs[1].iov_base+snd_iovecs[1].iov_len;
  snd_iovecs[2].iov_len=sizeof(message_tail_t);

  ipc_setup_buffer_pages(current_task(),rcv_iovecs,1,__buf_pages,bufs);

  ipc_transfer_buffer_data_iov(bufs,1,snd_iovecs,6,0,true);
  if( __validate_vectored_message(__vectored_msg_server_rcv_buf,4,tf) ) {
    tf->passed();
  } else {
    tf->failed();
  }

  /* 1-part buffer, 8-parts message. */
  tf->printf(SERVER_THREAD"Transferring 8 pieces of data to a 1-part IPC buffer.\n");
  CLEAR_SERVER_BUFFERS;
  __prepare_vectored_message(__vectored_msg_server_snd_buf,6);

  rcv_iovecs[0].iov_base=__vectored_msg_server_rcv_buf;
  rcv_iovecs[0].iov_len=6*sizeof(message_part_t)+sizeof(message_tail_t)+sizeof(message_header_t);

  snd_iovecs[0].iov_base=__vectored_msg_server_snd_buf;
  snd_iovecs[0].iov_len=sizeof(message_header_t);
  snd_iovecs[1].iov_base=snd_iovecs[0].iov_base+snd_iovecs[0].iov_len;
  snd_iovecs[1].iov_len=6*sizeof(message_part_t);
  snd_iovecs[2].iov_base=snd_iovecs[1].iov_base+snd_iovecs[1].iov_len;
  snd_iovecs[2].iov_len=sizeof(message_tail_t);

  tf->printf(SERVER_THREAD"Setting up buffer pages ... " );
  ipc_setup_buffer_pages(current_task(),rcv_iovecs,1,__buf_pages,bufs);
  tf->printf(" Done !\n" );

  tf->printf(SERVER_THREAD"Transferring data to the buffers ... " );
  ipc_transfer_buffer_data_iov(bufs,1,snd_iovecs,8,0,true);
  tf->printf(" Done !" );
  if( __validate_vectored_message(__vectored_msg_server_rcv_buf,6,tf) ) {
    tf->passed();
  } else {
    tf->failed();
  }

  /* 2-parts buffer, 8 pieces of data. */
  __parts=8;
  tf->printf(SERVER_THREAD"Transferring %d pieces of data to a 2-part IPC buffer.\n",
             __parts);
  CLEAR_SERVER_BUFFERS;
  __prepare_vectored_message(__vectored_msg_server_snd_buf,MIDDLE_PARTS(__parts));
  __setup_message_iovecs(__vectored_msg_server_snd_buf,MIDDLE_PARTS(__parts),snd_iovecs);

  rcv_iovecs[0].iov_base=__vectored_msg_server_rcv_buf;
  rcv_iovecs[0].iov_len=sizeof(message_header_t);
  rcv_iovecs[1].iov_base=rcv_iovecs[0].iov_base+rcv_iovecs[0].iov_len;
  rcv_iovecs[1].iov_len=6*sizeof(message_part_t)+sizeof(message_tail_t);

  tf->printf("Setting up buffer pages ... " );
  ipc_setup_buffer_pages(current_task(),rcv_iovecs,2,__buf_pages,bufs);
  tf->printf("Done !\n" );

  tf->printf("Transferring data to the buffer (PATTERN=0x%X) ... ",
             *(ulong_t *)snd_iovecs[1].iov_base);
  ipc_transfer_buffer_data_iov(bufs,2,snd_iovecs,__parts,0,true);
  tf->printf("Done !\n" );
  if( __validate_vectored_message(__vectored_msg_server_rcv_buf,MIDDLE_PARTS(__parts),tf) ) {
    tf->passed();
  } else {
    tf->failed();
  }

  /*
   * Testing IPC buffers data transfer using non-zero xfer offsets.
   */
  for(i=0;i<READ_LONGS;i++) {
    __zbuffer_snd[i]=i;
  }

  snd_iovecs[0].iov_base=__zbuffer_snd;
  snd_iovecs[0].iov_len=715*sizeof(long);
  snd_iovecs[1].iov_base=snd_iovecs[0].iov_base+snd_iovecs[0].iov_len;
  snd_iovecs[1].iov_len=9300*sizeof(long);
  snd_iovecs[2].iov_base=snd_iovecs[1].iov_base+snd_iovecs[1].iov_len;
  snd_iovecs[2].iov_len=sizeof(__zbuffer_snd)- snd_iovecs[0].iov_len- snd_iovecs[1].iov_len;

  r=ipc_setup_buffer_pages(current_task(),snd_iovecs,3,__buf_pages,bufs);
  if( r ) {
    tf->printf("Can't create buffers for offset reading !\n");
    tf->abort();
  }

  tf->printf("Size of pattern for IPC buffers read tests is %d bytes.\n",
             sizeof(__zbuffer_snd));
  for(i=0;i<NUM_OFFSETS;i++) {
    int j,op;
    __offset=__offsets[i]*sizeof(long);

    rcv_iovecs[0].iov_base=__zbuffer_rcv;
    rcv_iovecs[0].iov_len=sizeof(__zbuffer_snd)-__offset;
    memset(__zbuffer_rcv,0,sizeof(__zbuffer_rcv));

    p=(unsigned char *)rcv_iovecs[0].iov_base+rcv_iovecs[0].iov_len;
    *(ulong_t *)p=WL_PATTERN;

    tf->printf("Reading from buffer at offset %d (%d words of data) ... ",
               __offset,(sizeof(__zbuffer_snd)-__offset)/sizeof(long));
    r=ipc_transfer_buffer_data_iov(bufs,3,rcv_iovecs,1,__offset,false);

    if( r ) {
      tf->printf("\n"SERVER_THREAD"Failed to read data from buffer [1] : %d\n",r);
      tf->failed();
    } else {
      tf->printf("Done.\n");
    }

    if( *(ulong_t *)p != WL_PATTERN ) {
      tf->printf(SERVER_THREAD"[1] Watchline mismatch ! 0x%X instead of 0x%X !\n",
                 *(ulong_t *)p,WL_PATTERN);
      tf->failed();
    }

    lp=(long *)rcv_iovecs[0].iov_base;
    op=__offsets[i];
    tf->printf("    Verifying buffer (%d) words of data, start pattern= %d ... ",
               rcv_iovecs[0].iov_len/sizeof(long),op);
    for(j=0;j<rcv_iovecs[0].iov_len/sizeof(long);j++,lp++,op++) {
      if( *lp != op ) {
        tf->printf("\nMessage mismatch ! 0x%X instead of 0x%X at position %d.\n",
                   *lp,op,__offsets[i]+j);
        tf->failed();
      }
    }
    tf->printf(" OK.\n");
  }
  tf->printf(SERVER_THREAD"All IPC buffers functionality tests finished.\n");
}

static void __message_read_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int port=sys_create_port(IPC_BLOCKED_ACCESS,0);
  int r,i;
  port_msg_info_t msg_info;
  message_header_t *hdr;
  message_tail_t *tail;

  if( port < 0 ) {
    tf->printf( "Can't create a port !\n" );
    tf->abort();
  }

  __vectored_port=port;
  if( kernel_thread(__vectored_messages_thread,ctx,NULL) ) {
    tf->printf("Can't create the vectored messages tester !\n");
    tf->abort();
  }

  for(i=0;i<5*TEST_ROUNDS;i++) {
    ulong_t parts=(i%6)+1;
    ulong_t *watchline;
    ulong_t size;
    int j;

    memset(__zbuffer_rcv,0,sizeof(__zbuffer_rcv));
    size=sizeof(message_header_t);
    watchline=(ulong_t*)((char *)__zbuffer_rcv+size);
    *watchline=WL_PATTERN;

    tf->printf(SERVER_THREAD"RCV a message that has %d parts. SIZE=%d, NULL RCV buffer\n",
               parts,size);
    r=sys_port_receive(port,IPC_BLOCKED_ACCESS,(uintptr_t)__zbuffer_rcv,
                       sizeof(message_header_t),&msg_info);
    if( r ) {
      tf->printf(SERVER_THREAD"Error during receive ! %d.\n",r);
      tf->failed();
    }

    if( msg_info.msg_len != MESSAGE_SIZE(parts) ) {
      tf->printf(SERVER_THREAD"Bad message size ! %d instead of %d bytes.\n",
                 msg_info.msg_len,size);
      tf->failed();
    }

    if( *watchline != WL_PATTERN ) {
      tf->printf("[MSG READ] [abc] Watchline mismatch: 0x%X instead of 0x%X !\n",
                 *watchline,WL_PATTERN);
      tf->abort();
    }

    hdr=(message_header_t*)__zbuffer_rcv;
    r=__validate_message_data(hdr->data,hdr->data_base,MSG_HEADER_DATA_SIZE);
    if( r < 0 ) {
      tf->printf("First message part mismatch at %d\n",-r);
      tf->failed();
    }
    
    /* Now read the message partially. */
    memset(__zbuffer_rcv,0,sizeof(__zbuffer_rcv));
    size=sizeof(message_header_t);
    watchline=(ulong_t*)((char *)__zbuffer_rcv+size);
    *watchline=WL_PATTERN;

    r=sys_port_msg_read(port,msg_info.msg_id,(uintptr_t)__zbuffer_rcv,
                        size,0);
    if( r ) {
      tf->printf(SERVER_THREAD"Error during reading the message ! %d.\n",r);
      tf->failed();
    }

    if( *watchline != WL_PATTERN ) {
      tf->printf("[MSG READ] Watchline mismatch: 0x%X instead of 0x%X !\n",
                 *watchline,WL_PATTERN);
      tf->abort();
    }

    hdr=(message_header_t*)__zbuffer_rcv;
    r=__validate_message_data(hdr->data,hdr->data_base,MSG_HEADER_DATA_SIZE);
    if( r < 0 ) {
      tf->printf("First message part mismatch at %d\n",-r);
      tf->failed();
    }

    for(j=0;j<parts;j++) {
      memset(__zbuffer_rcv,0,sizeof(__zbuffer_rcv));
      message_part_t *part;
      size=sizeof(message_part_t);
      watchline=(ulong_t*)((char *)__zbuffer_rcv+size);
      *watchline=WL_PATTERN;

      r=sys_port_msg_read(port,msg_info.msg_id,(uintptr_t)__zbuffer_rcv,
                          size,sizeof(message_header_t)+j*sizeof(message_part_t));
      if( r ) {
        tf->printf(SERVER_THREAD"[xx] Error during reading the message ! %d.\n",r);
        tf->failed();
      }

      if( *watchline != WL_PATTERN ) {
        tf->printf("[MSG READ] Watchline mismatch: 0x%X instead of 0x%X !\n",
                   *watchline,WL_PATTERN);
        tf->abort();
      }

      part=(message_part_t*)__zbuffer_rcv;
      r=__validate_message_data(part->data,part->data_base,MSG_PART_DATA_SIZE);
      if( r < 0 ) {
        tf->printf("%d-th message part mismatch at %d\n",j+1,-r);
        tf->failed();
      }
    }

    memset(__zbuffer_rcv,0,sizeof(__zbuffer_rcv));
    size=sizeof(message_tail_t);
    watchline=(ulong_t*)((char *)__zbuffer_rcv+size);
    *watchline=WL_PATTERN;

    r=sys_port_msg_read(port,msg_info.msg_id,(uintptr_t)__zbuffer_rcv,
                        size,sizeof(message_header_t)+parts*sizeof(message_part_t));
    if( r ) {
      tf->printf(SERVER_THREAD"Error during reading message's tail ! %d.\n",r);
      tf->failed();
    }

    if( *watchline != WL_PATTERN ) {
      tf->printf("[MSG READ] Watchline mismatch: 0x%X instead of 0x%X !\n",
                 *watchline,WL_PATTERN);
      tf->abort();
    }

    tail=(message_tail_t*)__zbuffer_rcv;
    r=__validate_message_data(tail->data,tail->data_base,MSG_TAIL_DATA_SIZE);
    if( r < 0 ) {
      tf->printf("%message tail mismatch at %d\n",-r);
      tf->failed();
    }

    /* Now read message as one part. */
    r=sys_port_msg_read(port,msg_info.msg_id,(uintptr_t)__zbuffer_rcv,
                        MESSAGE_SIZE(parts),0);
    if( r ) {
      tf->printf(SERVER_THREAD"Error during whole 'READ' message ! %d.\n",r);
      tf->failed();
    }
    FAIL_ON(!__validate_vectored_message((uint8_t *)__zbuffer_rcv,parts,tf),tf);
    r=sys_port_reply(port,msg_info.msg_id,(uintptr_t)__zbuffer_rcv,MESSAGE_SIZE(parts));
    if( r ) {
      tf->printf(SERVER_THREAD"[READ MSG] Error during reply to message %d ! r=%d\n",
                 msg_info.msg_id,r);
      tf->abort();
    }
    tf->printf("Message header, tail and %d its parts validated.\n",parts);
  }
  sys_close_port(port);
}

static void __vectored_messages_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int port=sys_create_port(IPC_BLOCKED_ACCESS,0);
  task_t *task;
  int r,i;
  port_msg_info_t msg_info;
  iovec_t snd_iovecs[MAX_IOVECS];

  if( port < 0 ) {
    tf->printf( "Can't create a port !\n" );
    tf->abort();
  }

  __vectored_port=port;
  if( kernel_thread(__vectored_messages_thread,ctx,&task) ) {
    tf->printf("Can't create the vectored messages tester !\n");
    tf->abort();
  }

  for(i=0;i<5*TEST_ROUNDS;i++) {
    ulong_t parts=(i%6)+1;
    ulong_t *watchline;
    ulong_t size;

    CLEAR_SERVER_BUFFERS;

    /* Setup memory watchline. */
    size=MESSAGE_SIZE(parts);
    watchline=(ulong_t*)(__vectored_msg_server_rcv_buf+size);
    *watchline=WL_PATTERN;

    tf->printf(SERVER_THREAD"Receiving a message that has %d middle parts. SIZE=%d\n",
               parts,MESSAGE_SIZE(parts));
    r=sys_port_receive(port,IPC_BLOCKED_ACCESS,(ulong_t)__vectored_msg_server_rcv_buf,
                       sizeof(__vectored_msg_server_rcv_buf),&msg_info);
    tf->printf(SERVER_THREAD"Vectored message received. MESSAGE SIZE=%d\n",
               msg_info.msg_len);
    __validate_retval(r,0,tf);
    FAIL_ON(!__validate_vectored_message(__vectored_msg_server_rcv_buf,parts,tf),tf);

    if( *watchline != WL_PATTERN ) {
      tf->printf(SERVER_THREAD"Watchline pattern mismatch ! 0x%X instead of 0x%X\n",
                 *watchline,WL_PATTERN);
      tf->failed();
    }

    /* Reply to the client using a vectored message. */
    CLEAR_SERVER_BUFFERS;
    __prepare_vectored_message(__vectored_msg_server_snd_buf,parts);
    __setup_message_iovecs(__vectored_msg_server_snd_buf,parts,snd_iovecs);

    tf->printf(SERVER_THREAD"Replying by a message that consists of %d parts.\n",
               parts);
    r=sys_port_reply_iov(port,msg_info.msg_id,snd_iovecs,parts+2);
    tf->printf(SERVER_THREAD"Message %d was replied (%d)\n",
               msg_info.msg_id,r);
  }
  tf->printf(SERVER_THREAD "All messages were successfully transmitted.\n");

  if( sys_close_port(port) ) {
    tf->printf(SERVER_THREAD"Can't close the port used for vectored messages tests !\n");
    tf->abort();
  }
}

#define __NGROUPS 3
#define __NGROUP_TASKS  3
#define __NUM_PRIO_THREADS (__NGROUPS*__NGROUP_TASKS)

int __prio_port;

typedef struct __prio_data {
  test_framework_t *tf;
  ulong_t priority,runs;
} prio_data_t;

#define PRIORER_ID "[PRIO CLIENT]"

static uint8_t __buf114_snd[MAX_MSG_SIZE];
static uint8_t __buf114_rcv[MAX_MSG_SIZE];

static void __prio_thread(void *data)
{
  prio_data_t *pd=(prio_data_t *)data;
  test_framework_t *tf=pd->tf;
  int r,i;
  iovec_t snd_iovecs[MAX_IOVECS],rcv_iovecs[MAX_IOVECS];
  ulong_t channel;
  ulong_t size,prio,parts;
  uint8_t *_sbuf,*_rbuf;

  channel=sys_open_channel(__server_pid,__prio_port,IPC_BLOCKED_ACCESS);
  if( channel < 0 ) {
    tf->printf(PRIORER_ID"Can't open a channel !\n" );
    tf->abort();
  }

  prio=pd->priority;
  r=do_scheduler_control(current_task(),SYS_SCHED_CTL_SET_PRIORITY,prio);
  if( r ) {
    tf->printf(PRIORER_ID"Can't change my priority to %d ! r=%d\n",
               prio,r);
    tf->abort();
  }
  tf->printf(PRIORER_ID "[CPU %d:%d] Successfully changed priority to %d. [%d]\n",
             cpu_id(),current_task()->pid,prio,
             current_task()->static_priority);

  parts=3;
  for(i=0;i<TEST_ROUNDS;i++) {
    ulong_t *watchline;

    if( !cpu_id() ) {
      CLEAR_CLIENT_BUFFERS;
      _sbuf=__vectored_msg_client_snd_buf;
      _rbuf=__vectored_msg_client_rcv_buf;
    } else {
      memset(__buf114_snd,0,sizeof(__buf114_snd));
      memset(__buf114_snd,0,sizeof(__buf114_snd));
      _sbuf=__buf114_snd;
      _rbuf=__buf114_rcv;
    }

    __prepare_vectored_message(_sbuf,parts);
    __setup_message_iovecs(_sbuf,parts,snd_iovecs);

    /* Setup memory watchline. */
    size=MESSAGE_SIZE(parts);
    watchline=(ulong_t*)(_sbuf+size);
    *watchline=WL_PATTERN;

    tf->printf(PRIORER_ID "[CPU %d:%d] Sending a message of %d parts via %s. SIZE=%d\n",
               cpu_id(),current_task()->pid,
               parts, (i & 0x1) ? "'sys_port_send_iov()'" : "'sys_port_send_iov_v()'",
               MESSAGE_SIZE(parts));
    if( i & 0x1 ) {
      r=sys_port_send_iov(channel,snd_iovecs,parts+2,
                          (uintptr_t)_rbuf,
                          MAX_MSG_SIZE);
      tf->printf(PRIORER_ID"Message was sent: r=%d. RCV BUFSIZE=%d\n",r,
                 MAX_MSG_SIZE);
      if( r < 0 ) {
        tf->failed();
      }
    } else {
      __setup_message_iovecs(_rbuf,parts,rcv_iovecs);
      r=sys_port_send_iov_v(channel,snd_iovecs,parts+2,
                            rcv_iovecs,parts+2);
      tf->printf(PRIORER_ID"Message was sent: r=%d. RCV BUFSIZE=%d\n",r,
                 MAX_MSG_SIZE);
      if( r < 0 ) {
        tf->failed();
      }
    }
    tf->printf(PRIORER_ID"Verifying server's reply (%d-parts message).\n",
               parts);
    FAIL_ON(!__validate_vectored_message(_rbuf,parts,tf),tf);
    if( *watchline != WL_PATTERN ) {
      tf->printf(VECTORER_ID"Watchline pattern mismatch ! 0x%X instead of 0x%X\n",
                 *watchline,WL_PATTERN);
      tf->failed();
    }
  }

  sys_exit(0);
}

static prio_data_t pdata[__NUM_PRIO_THREADS];
static task_t *ptasks[__NUM_PRIO_THREADS];

#define __OVF_DATA_SIZE 137
typedef struct __ovf_data {
  unsigned char data[__OVF_DATA_SIZE];
} ovf_data_t;

typedef struct __ovf_header {
  ulong_t f1,f2;
} ovf_header_t;

static ovf_data_t __master_ovf;
static ovf_header_t __header_ovf={0x99999999AAAAAAAA,
                                  0xBBBBBBBBCCCCCCCC};

#define __CP 0xFE
#define __CP_BUFSIZE  31

static long __ovf_port;

static void __stack_overflow_client(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int r;
  unsigned char d[sizeof(ovf_header_t)+2];
  unsigned char *p1=&d[0];
  ovf_header_t *ovf_header=(ovf_header_t*)&d[1];
  unsigned char *p2=&d[sizeof(d)-1];

  unsigned char e[sizeof(ovf_data_t)+2];
  unsigned char *p3=&e[0];
  ovf_data_t *ovf_data=(ovf_data_t *)&e[1];
  unsigned char *p4=&e[sizeof(e)-1];

  iovec_t s_vec,r_vec[2];
  long channel;
  int i;

  channel=sys_open_channel(__server_pid,__vectored_port,IPC_BLOCKED_ACCESS);
  if( channel < 0 ) {
    tf->printf("[OVF TESTER]: Can't open a channel !\n" );
    tf->abort();
  }

  tf->printf("[OVF TESTER]: Sending message ...\n" );

  *p1=__CP;
  *p2=__CP;
  *p3=__CP;
  *p4=__CP;

  *ovf_header=__header_ovf;

  s_vec.iov_base=ovf_header;
  s_vec.iov_len=sizeof(*ovf_header);

  r_vec[0]=s_vec;
  r_vec[1].iov_base=ovf_data;
  r_vec[1].iov_len=sizeof(*ovf_data);

  r=sys_port_send_iov_v(channel,&s_vec,1,r_vec,2);
  tf->printf("[OVF TESTER]: Got a response: %d bytes\n",
             r);
  if( r < 0 || r != (r_vec[0].iov_len + r_vec[1].iov_len ) ) {
    tf->printf("[OVF TESTER]: Message had to be %d bytes length.\n",
               r_vec[0].iov_len + r_vec[1].iov_len);
    tf->failed();
  }

  if( *p1 != __CP ) {
    kprintf("[!!] P1 mismatch ! %p instead of %p\n",
            *p1,__CP);
    tf->abort();
  }

  if( *p2 != __CP ) {
    kprintf("[!!] P2 mismatch ! %p instead of %p\n",
            *p2,__CP);
    tf->abort();
  }

  if( *p3 != __CP ) {
    kprintf("[!!] P3 mismatch ! %p instead of %p\n",
            *p3,__CP);
    tf->abort();
  }

  if( *p4 != __CP ) {
    kprintf("[!!] P4 mismatch ! %p instead of %p\n",
            *p4,__CP);
    tf->abort();
  }

  tf->printf("[OVF TESTER]: Verifying header ...\n");
  if( ovf_header->f1 != WL_PATTERN ||
      ovf_header->f2 != WL_PATTERN*2 ) {
    tf->printf("[OVF TESTER]: Insufficient header !\n");
  }
  tf->printf("OK\n");
  
  tf->printf("[OVF TESTER]: Verifying the message ...\n");
  for(i=0;i<__OVF_DATA_SIZE;i++) {
    if( ovf_data->data[i] != 0x15 ) {
      tf->printf("[OVF TESTER]: Bad data at %d position: 0x%X\n",
                 i,ovf_data->data[i]);
      tf->failed();
    }
  }
  tf->printf("[OVF TESTER]: Test passed\n");
  sys_exit(0);
}

static void __stack_overflow_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int r,i;
  unsigned char d[__CP_BUFSIZE+2];
  unsigned char *p1=&d[0];
  unsigned char *dst=&d[1];
  unsigned char *p2=&d[sizeof(d)-1];
  int to_copy=p2-p1-1;  
  port_msg_info_t msg_info;
  iovec_t iov[2];
  ovf_header_t header=__header_ovf,i_header;
  ovf_data_t o_data;

  *p1=__CP;
  *p2=__CP;
  kprintf("p1=%p\n",p1);
  kprintf("d=[%p,size=%d], last address=%p\n",
          &d[1],to_copy,(ulong_t)&d[1]+to_copy-1);
  kprintf("p2=%p\n",p2);
  copy_to_user(dst,&__master_ovf,to_copy);

  if( *p1 != __CP ) {
    kprintf("[!!] P1 mismatch ! %p instead of %p\n",
            *p1,__CP);
    tf->abort();
  }
  if( *p2 != __CP ) {
    kprintf("[!!] P2 mismatch ! %p instead of %p\n",
            *p2,__CP);
    tf->abort();
  }

  kprintf( "** Raw usercopy tests finished !\n" );
  __ovf_port=sys_create_port(IPC_BLOCKED_ACCESS,0);
  if( __ovf_port < 0 ) {
    tf->printf(SERVER_THREAD"Can't create a port for stack overflow tests !\n");
    tf->abort();
  }

  if( kernel_thread(__stack_overflow_client,ctx,NULL) ) {
    tf->printf(SERVER_THREAD"Can't create the overflow client !\n");
    tf->abort();
  }

  tf->printf("[OVF TEST]: Waiting for incoming messages ...\n");
  r=sys_port_receive(__ovf_port,IPC_BLOCKED_ACCESS,(ulong_t)&i_header,
                     sizeof(i_header),&msg_info);
  if( r < 0 ) {
    tf->printf("[OVF TEST]: Error wuring receiving a message ! %d\n",
               r);
    tf->abort();
  }
  tf->printf("[OVF TEST]: Got a message %d from %d of %d bytes length!\n",
             msg_info.msg_id,msg_info.sender_pid,
             msg_info.msg_len);
  if( msg_info.msg_len != sizeof(i_header) ) {
    tf->printf("[OVF TEST]: Insufficient message size: %d instead of %d\n",
               msg_info.msg_len,sizeof(i_header));
    tf->failed();
  }

  if( memcmp(&i_header,&__header_ovf,sizeof(i_header)) ) {
    tf->printf("[OVF TEST]: Insufficient message data !\n");
    tf->failed();
  }

  tf->printf("[OVF TEST]: Replying by a 2-part message.\n");
  for(i=0;i<__OVF_DATA_SIZE;i++) {
    o_data.data[i]=0x15;
  }

  header.f1=WL_PATTERN;
  header.f2=WL_PATTERN*2;
  
  iov[0].iov_base=&header;
  iov[0].iov_len=sizeof(header);

  iov[1].iov_base=&o_data;
  iov[1].iov_len=sizeof(o_data)+10;

  r=sys_port_reply_iov(__ovf_port,msg_info.msg_id,
                       iov,2);
  if( r ) {
    tf->printf(SERVER_THREAD"[PRIO PORT] Error during reply to message %d ! r=%d\n",
               msg_info.msg_id,r);
    tf->abort();
  }

  tf->printf("[OVF TEST]: All tests finished.\n");
  sys_close_port(__ovf_port);
}

static void __prioritized_port_test(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int r;
  int i,j,k;
  port_msg_info_t msg_info;
  iovec_t snd_iovecs[MAX_IOVECS];
  const int __PRIO_STEP=4;
  int prio,parts;
  static int PRIO_BASE=64;
  struct __ipc_gen_port *genport;

  /* Only one task from the most prioritized group can initially be
   * in the port's message queue. This will give us a better test conditions.
   */
  __prio_port=sys_create_port(IPC_PRIORITIZED_ACCESS | IPC_BLOCKED_ACCESS,
                              __NUM_PRIO_THREADS-(__NGROUP_TASKS-1));
  if( __prio_port < 0 ) {
    tf->printf("Can't create prioritized port !");
    tf->abort();
  }

  for(i=0;i<__NUM_PRIO_THREADS;i++) {
    prio=PRIO_BASE-__PRIO_STEP*(i/__NGROUP_TASKS);
    pdata[i].tf=tf;
    pdata[i].priority=prio;
    pdata[i].runs=0;

    if( kernel_thread(__prio_thread,&pdata[i],&ptasks[i]) ) {
      tf->printf("Can't create a prio tester !");
      tf->abort();
    }
  }

  tf->printf(SERVER_THREAD"[PRIO PORT] Sleeping for a while ...\n");
  sleep(HZ/3);
  tf->printf(SERVER_THREAD"[PRIO PORT] Got woken up ! Testing ! (start priority=%d)\n",
             prio);

  /* OK, ready for tests. */
  parts=3;
  for(k=0;k<__NGROUPS;k++,prio+=__PRIO_STEP) {
    for(j=0;j<__NGROUP_TASKS;j++) {
      for(i=0;i<TEST_ROUNDS;i++) {
        ulong_t *watchline;
        ulong_t size;
        task_t *t;
        int idx;
        prio_data_t *_pd;

        CLEAR_SERVER_BUFFERS;

        /* Setup memory watchline. */
        size=MESSAGE_SIZE(parts);
        watchline=(ulong_t*)(__vectored_msg_server_rcv_buf+size);
        *watchline=WL_PATTERN;

        tf->printf(SERVER_THREAD"[PRIO PORT] Receiving a message that has %d middle parts. SIZE=%d\n",
                   parts,MESSAGE_SIZE(parts));
        msg_info.msg_id = 666;
        r=sys_port_receive(__prio_port,IPC_BLOCKED_ACCESS,(ulong_t)__vectored_msg_server_rcv_buf,
                           sizeof(__vectored_msg_server_rcv_buf),&msg_info);
        tf->printf(SERVER_THREAD"[PRIO PORT]Vectored message received. MESSAGE SIZE=%d\n",
                   msg_info.msg_len);
        __validate_retval(r,0,tf);
        FAIL_ON(!__validate_vectored_message(__vectored_msg_server_rcv_buf,parts,tf),tf);

        if( *watchline != WL_PATTERN ) {
          tf->printf(SERVER_THREAD"[PRIO PORT] Watchline pattern mismatch ! 0x%X instead of 0x%X\n",
                     *watchline,WL_PATTERN);
          tf->failed();
        }

        /* Make sure client has a proper priority. */
        for(t=NULL,idx=0;idx<__NUM_PRIO_THREADS;idx++) {
          if(ptasks[idx]->pid == msg_info.sender_pid) {
            t=ptasks[idx];
            _pd=&pdata[idx];
            break;
          }
        }

        if( !t ) {
          tf->printf(SERVER_THREAD"[PRIO PORT] Can't locate a client with PID=%d\n",
                     msg_info.sender_pid);
          tf->abort();
        }

        /* Make sure kernel provided us with sender's TID and UID properly. */
	if( msg_info.sender_tid != t->tid || msg_info.sender_uid != t->uid ) {
          tf->printf(SERVER_THREAD"[PRIO PORT] Wrong sender info ! TID (%d:%d), UID (%d:%d)\n",
                     t->tid,msg_info.sender_tid,t->uid,msg_info.sender_uid);
          tf->abort();
	}

        /* OK, now compare priority */
        if( t->static_priority != prio ) {
          tf->printf(SERVER_THREAD"[PRIO PORT]: Priority mismatch ! %d instead of %d\n",
                     t->static_priority,prio);
          tf->printf("             CURRENT ITERATION: round=%d,task=%d,group=%d\n",
                     i,j,k);
          tf->abort();
        }

        tf->printf(SERVER_THREAD"[PRIO PORT] Good client: MSG ID=%d, PRIO=%d\n",
                   msg_info.msg_id,t->static_priority);

        /* Reply to the client using a vectored message. */
        CLEAR_SERVER_BUFFERS;
        __prepare_vectored_message(__vectored_msg_server_snd_buf,parts);
        __setup_message_iovecs(__vectored_msg_server_snd_buf,parts,snd_iovecs);

        tf->printf(SERVER_THREAD"[PRIO PORT]Replying by a message that consists of %d parts.\n",
                   parts);
        r=sys_port_reply_iov(__prio_port,msg_info.msg_id,snd_iovecs,parts+2);
        tf->printf(SERVER_THREAD"[PRIO PORT]Message %d was replied (%d)\n",
                   msg_info.msg_id,r);
        if( r ) {
          tf->printf(SERVER_THREAD"[PRIO PORT] Error during reply to message %d ! r=%d\n",
                     msg_info.msg_id,r);
          tf->abort();
        }
      }
    }
  }

  genport=__ipc_get_port(current_task(),__prio_port);
  if( !genport ) {
    tf->printf(SERVER_THREAD"Can't resolve port !");
    tf->abort();
  }

  if( genport->avail_messages || genport->total_messages ) {
    tf->printf(SERVER_THREAD"Error ! Port state is insufficient! Avail: %d, Total: %d\n",
               genport->avail_messages,genport->total_messages);
    tf->abort();
  }

  tf->printf(SERVER_THREAD"All priority-related tests finished.\n");
  sys_close_port(__prio_port);
  sleep(HZ);
}

static void __server_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int ports[SERVER_NUM_PORTS];
  int i;
  ulong_t flags;
  int r;
  port_msg_info_t msg_info;

  __server_pid=current_task()->pid;

#ifdef CONFIG_SMP
  kthread_cpu_autodeploy=true;
#endif

  __stack_overflow_test(ctx);
  sleep(HZ);
  __ipc_buffer_test(ctx);
  sleep(HZ);
  __process_events_test(ctx);
  sleep(HZ);
  __message_read_test(ctx);
  sleep(HZ);
  __prioritized_port_test(ctx);
  sleep(HZ);
  __vectored_messages_test(ctx);
  sleep(HZ);

  for( i=0;i<SERVER_NUM_PORTS;i++) {
    if( i != NON_BLOCKED_PORT_ID ) {
      flags=IPC_BLOCKED_ACCESS;
    } else {
      flags=0;
    }
    ports[i]=sys_create_port(flags,0);
    if( ports[i] != i ) {
      tf->printf(SERVER_THREAD "IPC port number mismatch: %d instead of %d\n",
                 ports[i],i);
      tf->failed();
      goto exit_test;
    }
  }

  tctx->server_pid=current_task()->pid;
  tf->printf(SERVER_THREAD "%d ports created.\n", SERVER_NUM_PORTS );

  goto poll_tests;

  if( kernel_thread(__client_thread,ctx,NULL) ) {
    tf->printf(SERVER_THREAD "Can't launch client thread.\n",
               ports[i],i);
    goto abort_test;
  }

  /********************************************************
   * Testing message delivery in blocking mode.
   ********************************************************/
  tf->printf(SERVER_THREAD "Testing message delivery in blocking mode.\n" );
  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_receive(i,IPC_BLOCKED_ACCESS,(ulong_t)__server_rcv_buf,
                       sizeof(__server_rcv_buf),&msg_info);
    tf->printf(SERVER_THREAD "Got a message !\n");
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    } else {
      if( msg_info.msg_id != 0 ) {
        tf->printf(SERVER_THREAD "Insufficient message id: %d instead of 0\n",
                   msg_info.msg_id);
        tf->failed();
      } else {
        if( msg_info.msg_len == strlen(patterns[i])+1 ) {
          if( !__verify_message(i,__server_rcv_buf) ) {
            tf->printf( SERVER_THREAD "Message N %d mismatch.\n",
                        i );
            tf->failed();
          } else {
            tf->printf( SERVER_THREAD "Message N %d was successfuly transmitted.\n",
                        i );
            tf->passed();
          }
        } else {
          tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                     i,msg_info.msg_len,strlen(patterns[i])+1);
          tf->failed();
        }
      }
    }
    tf->printf(SERVER_THREAD"Replying to %d with %d bytes of data\n",
               i,strlen(patterns[i])+1);
    r=sys_port_reply(i,0,(ulong_t)patterns[i],strlen(patterns[i])+1);
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_reply': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    }
  }

  /****************************************************************
   * Testing delivery of big messages in blocking mode.
   ****************************************************************/
  tf->printf(SERVER_THREAD "Testing delivery of a big message (%d bytes).\n",
             BIG_MESSAGE_SIZE);
  r=sys_port_receive(BIG_MESSAGE_PORT_ID,IPC_BLOCKED_ACCESS,
                     (ulong_t)__big_message_server_buf,
                     BIG_MESSAGE_SIZE,&msg_info);
  if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
  } else {
    kprintf( SERVER_THREAD "Big message received: %d bytes.\n",msg_info.msg_len );
    if( msg_info.msg_len == BIG_MESSAGE_SIZE ) {
      ulong_t offt;

      if( !__verify_big_message(__big_message_server_buf,&offt) ) {
        tf->printf( SERVER_THREAD "Big message mismatch at offset: %d\n",
                    offt);
        tf->failed();
      } else {
        tf->printf( SERVER_THREAD "Big message was successfully transmitted.\n" );
        tf->passed();
      }
    } else {
        tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                   msg_info.msg_id,msg_info.msg_len,BIG_MESSAGE_SIZE);
        tf->failed();
    }
  }
  r=sys_port_reply(BIG_MESSAGE_PORT_ID,msg_info.msg_id,
                   (ulong_t)__big_message_pattern,BIG_MESSAGE_SIZE);
  if( r ) {
    tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_reply': %d\n",
               r);
    tf->failed();
    goto exit_test;
  }

  /*****************************************************************
   * Testing message delivery in non-blocking mode (small messages).
   ****************************************************************/
  sleep(HZ/2);
  tf->printf(SERVER_THREAD "Testing message delivery in non-blocking mode.\n" );

  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_receive(NON_BLOCKED_PORT_ID,IPC_BLOCKED_ACCESS,
                       (ulong_t)__server_rcv_buf,
                       sizeof(__server_rcv_buf),&msg_info);
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    } else {
      if( msg_info.msg_len == strlen(patterns[i])+1 ) {
        if( !__verify_message(i,__server_rcv_buf) ) {
          tf->printf( SERVER_THREAD "Message N %d mismatch.\n",i );
          tf->failed();
        } else {
          tf->printf( SERVER_THREAD "Non-blocking Message N %d successfully received.\n",i );
          tf->passed();
        }
      } else {
        tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                   i,msg_info.msg_len,strlen(patterns[i])+1);
        tf->failed();
      }
    }
  }

poll_tests:
  /*****************************************************************
   * Testing port polling.
   ****************************************************************/
  tf->printf(SERVER_THREAD "Testing IPC port polling.\n" );
  __ipc_poll_test(tctx,ports);

  goto exit_test;
abort_test:
  tf->abort();
exit_test:
  tctx->tests_finished=true;
  sys_exit(0);
}

static void __initialize_big_pattern(void)
{
  int i;
  int *p=(int *)__big_message_pattern;

  for(i=0;i<BIG_MESSAGE_SIZE/sizeof(int);i++) {
    *p++=i;
  }
}

static bool __ipc_tests_initialize(void **ctx)
{
  ipc_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    tctx->tests_finished=false;
    tctx->server_pid=10000;
    *ctx=tctx;
    __initialize_big_pattern();
    return true;
  }
  return false;
}

void __ipc_tests_run(test_framework_t *f,void *ctx)
{
  ipc_test_ctx_t *tctx=(ipc_test_ctx_t*)ctx;
  
  tctx->tf=f;

  if( kernel_thread(__server_thread,tctx,NULL) ) {
    f->printf( "Can't create server thread !" );
    f->abort();
  } else {
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}

void __ipc_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t ipc_testcase={
  .id=TEST_ID,
  .initialize=__ipc_tests_initialize,
  .deinitialize=__ipc_tests_deinitialize,
  .run=__ipc_tests_run,
};
