#ifndef __IPC_H__
#define  __IPC_H__

#include <arch/types.h>
#include <sync/mutex.h>
#include <arch/atomic.h>
#include <ds/idx_allocator.h>
#include <sync/spinlock.h>
#include <arch/arch_ipc.h>
#include <ipc/channel.h>
#include <ipc/port.h>

/* Blocking mode */
#define IPC_BLOCKED_ACCESS  0x01
#define IPC_AUTOREF         0x02
#define IPC_KERNEL_SIDE     0x04

/**< Maximum numbers of vectors for I/O operations. */
#define MAX_IOVECS  8

#define UNTRUSTED_MANDATORY_FLAGS  (IPC_BLOCKED_ACCESS)

#define IPC_DEFAULT_PORT_MESSAGES  512
#define IPC_MAX_PORT_MESSAGES  512
#define IPC_DEFAULT_BUFFERS 512

/**< Number of pages statically allocated for a task for its large messages. */
#define IPC_PERTASK_PAGES 1

typedef struct __ipc_cached_data {
  void *cached_page1, *cached_page2;
  ipc_port_message_t cached_port_message;
} ipc_cached_data_t;

typedef struct __ipc_pstats {
  int foo[4];
} ipc_pstats_t;

typedef struct __task_ipc {
  mutex_t mutex;
  atomic_t use_count;  /* Number of tasks using this IPC structure. */

  /* port-related stuff. */
  ulong_t num_ports,max_port_num;
  ipc_gen_port_t **ports;
  idx_allocator_t ports_array;
  int allocated_ports;
  spinlock_t port_lock;

  /* Channels-related stuff. */
  idx_allocator_t channel_array;
  ulong_t num_channels,max_channel_num;
  ipc_channel_t **channels;
  spinlock_t channel_lock;
  int allocated_channels;
} task_ipc_t;

typedef struct __task_ipc_priv {
  /* Cached singletones for synchronous operations. */
  ipc_cached_data_t cached_data;
  ipc_pstats_t pstats;
} task_ipc_priv_t;

int setup_task_ipc(task_t *task);
void release_task_ipc_priv(task_ipc_priv_t *priv);

#define LOCK_IPC(ipc)  mutex_lock(&(ipc)->mutex)
#define UNLOCK_IPC(ipc) mutex_unlock(&(ipc)->mutex);

#define IPC_LOCK_PORTS(ipc) spinlock_lock(&ipc->port_lock)
#define IPC_UNLOCK_PORTS(ipc) spinlock_unlock(&ipc->port_lock)

#define IPC_LOCK_BUFFERS(ipc) spinlock_lock(&ipc->buffer_lock);
#define IPC_UNLOCK_BUFFERS(ipc) spinlock_unlock(&ipc->buffer_lock);

#define IPC_LOCK_CHANNELS(ipc) spinlock_lock(&ipc->channel_lock)
#define IPC_UNLOCK_CHANNELS(ipc) spinlock_unlock(&ipc->channel_lock)

#define REF_IPC_ITEM(c)  atomic_inc(&c->use_count)
#define UNREF_IPC_ITEM(c)  atomic_dec(&c->use_count)

void release_task_ipc(task_ipc_t *ipc);
void *allocate_ipc_memory(long size);
void free_ipc_memory(void *addr,int size);

typedef struct __iovec {
  void *iov_base;
  size_t iov_len;
} iovec_t;

static inline task_ipc_t *get_task_ipc(task_t *t)
{
  task_ipc_t *ipc;

  LOCK_TASK_MEMBERS(t);
  if( t->ipc ) {
    atomic_inc(&t->ipc->use_count);
    ipc=t->ipc;
  } else {
    ipc=NULL;
  }
  UNLOCK_TASK_MEMBERS(t);

  return ipc;
}

/**
 * @fn status_t sys_create_port( ulong_t flags, ulong_t queue_size )
 * @brief Create an IPC port.
 *
 * This system call creates new IPC port for the calling process using
 * target flags. The port will have queue size equal to the @a queue_size.
 * After a new port have been successfully created, it can be used for
 * receiving data.
 *
 * @param flags - Flags that control properties of a new port.
 *    Currently no flags supported et all, so this parameter is ignored.
 * @queue_size - size of the message queue for the port. If this value
 *    exceeds the maximum port queue size allowed for the calling process,
 *    this function will fail. Passing zero as queue size tells the kernel
 *    to use the default port queue size.
 *
 * @return If a new port was successfully created this function returns
 * its descriptor (a small positive/zero number). In case of failure,
 * negations of the following error codes are returned:
 *    EMFILE - calling process has reached the maximum number of allowed
 *             IPC ports.
 *    ENOMEM - memory allocation error occured during port allocation.
 *    ERERM  - calling process wasn't allowed to create a new IPC port.
 *    EINVAL - insufficient flags passed.
 */
long sys_create_port( ulong_t flags, ulong_t queue_size );

/**
 * @fn status_t sys_port_send(pid_t pid,ulong_t port,uintptr_t snd_buf,
 *                            ulong_t snd_size,uintptr_t rcv_buf,ulong_t rcv_size)
 * @brief Synchronously send data to target port.
 *
 * This system call sends data to target port over the channel.
 *
 * @param channel Identificator of the channel.
 * @param snd_buf Pointer to the buffer that contains data to be sent.
 * @param snd_size Number of bytes to be sent.
 * @param rcv_buf Pointer to buffer that will contain reply data.
 * @param rcv_size Size of the receive buffer.
 *
 * @return After successful data transfer, this function returns amount
 * of bytes in the server reply message. Otherwise, negations of
 * the following errors are returned:
 *    ESRCH - Insufficient pid was used.
 *    EFAULT - Insufficient memory address was passed.
 *    EBUSY  - No free space in target port for storing this request.
 *    ENOMEM - Memory allocation error occured while processing this
 *             request.
 *    DEADLOCK - Sender is trying to send message to itself.
 *    EINVAL - This return is returned upon the following conditions:
 *             a) insufficient port number was provided;
 *             b) message had insufficient size (currently only up to 2MB
 *                can be transferred via ports).
 */
long sys_port_send(ulong_t channel,
                   uintptr_t snd_buf, size_t snd_size,
                   uintptr_t rcv_buf, size_t rcv_size);

/**
 * @fn status_t sys_port_receive( ulong_t port, ulong_t flags, ulong_t recv_buf,
 *                                ulong_t recv_len)
 * @brief Receives data from target port.
 *
 * This system call receives data from target port and stores it to target
 * buffer. This function won't put the calling process into sleep
 * (until available messages appear) unless a special flag is specified.
 *
 * @param port Target port that belongs to the calling process.
 * @param recv Output buffer where to put received data.
 * @param recv_len Size of output buffer.
 * @param msg_info Structure that will contain message details.
 *
 * @return Upon successful reception of a new message, this function
 *  returns zero and fills in the structure.
 *  Otherwise, negations of the following errors are returned:
 *     EINVAL - insufficient port number, or NULL buffer address,
 *              or zero receive buffer length was provided.
 *     EFAULT - Insufficient memory address was passed.
 *     EWOULDBLOCK - Non-blocking access was requested and there are
 *                   no messages available in the queue.
 */
long sys_port_receive(ulong_t port, ulong_t flags, ulong_t recv_buf,
                      size_t recv_len, port_msg_info_t *msg_info);

/**
 * @fn status_t sys_port_reply(ulong_t port, ulong_t msg_id,ulong_t reply_buf,
 *                             ulong_t reply_len)
 * @brief Reply to port message.
 *
 * This system call replies to target port message which means the following:
 *   a) copy reply data to the sender (i.e. the process, that has sent
 *      the message);
 *   b) wake up the sender;
 *
 * @param port The port used for message reception.
 * @param msg_id Message ID.
 * @param reply_buf Buffer that contains data to reply.
 * @param reply_len Number of bytes to send.
 */
size_t sys_port_reply(ulong_t port, ulong_t msg_id,ulong_t reply_buf,
                      ulong_t reply_len);

long replicate_ipc(task_ipc_t *source,task_t *rcpt);
void initialize_ipc(void);

long sys_open_channel(pid_t pid,ulong_t port,ulong_t flags);
int sys_close_channel(ulong_t channel);
int sys_close_port(ulong_t port);
long sys_control_channel(ulong_t channel,ulong_t cmd,ulong_t arg);
long sys_port_send_iov(ulong_t channel,
                       iovec_t iov[], uint32_t numvecs,
                       uintptr_t rcv_buf, ulong_t rcv_size);
long sys_port_send_iov_v(ulong_t channel,
                             iovec_t snd_iov[],ulong_t snd_numvecs,
                             iovec_t rcv_iov[],ulong_t rcv_numvecs);
long sys_port_reply_iov(ulong_t port, ulong_t msg_id,
                        iovec_t reply_iov[], uint32_t numvecs);
long sys_port_msg_read(ulong_t port, ulong_t msg_id, uintptr_t recv_buf,
                       size_t recv_len, off_t offset);


#endif
