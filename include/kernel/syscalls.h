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
 */

#ifndef __K_SYSCALLS_H__
#define __K_SYSCALLS_H__

#ifndef __ASM__

#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <ipc/port.h>
#include <eza/time.h>
#include <ipc/poll.h>
#include <ipc/gen_port.h>
#include <eza/sync.h>

#endif

/* Syscalls identificators. */
#define SC_GET_PID             0
#define SC_CREATE_TASK         1
#define SC_TASK_CONTROL        2
#define SC_MMAP                3
#define SC_CREATE_PORT         4
#define SC_PORT_SEND           5
#define SC_PORT_RECEIVE        6
#define SC_PORT_REPLY          7
#define SC_ALLOCATE_IOPORTS    8
#define SC_FREE_IOPORTS        9
#define SC_CREATE_IRQ_ARRAY    10
#define SC_WAIT_ON_IRQ_ARRAY   11
#define SC_IPC_PORT_POLL       12
#define SC_NANOSLEEP           13
#define SC_SCHED_CONTROL       14
#define SC_GET_TID             15
#define SC_EXIT                16
#define SC_OPEN_CHANNEL        17
#define SC_CLOSE_CHANNEL       18
#define SC_CLOSE_PORT          19
#define SC_CONTROL_CHANNEL     20
#define SC_PORT_SEND_IOV       21
#define SC_SYNC_CREATE         22
#define SC_SYNC_CONTROL        23
#define SC_SYNC_DESTROY        24
#define SC_KILL                25
#define SC_SIGNAL              26
#define SC_SIGRETURN           27

#ifndef __ASM__

/**
 * @fn status_t sys_get_pid(void)
 * @return PID of the calling process.
 */
status_t sys_get_pid(void);


/**
 * @fn status_t sys_create_task(task_creation_flags_t flags)
 * @brief Create a new task (object for scheduling).
 *
 * This routine is the kernel main routine for creating new threads.
 * The following conditions are met during kernel creation:
 *   - new thread will have its own unique PID;
 *   - the caling thread will be the parent of the new thread;
 *   - new thread will have its own kernel stack;
 *   - CPU ID of new thread will be equal to its parent's;
 *   - new thread's state will be 'TASK_STATE_JUST_BORN';
 *   - new thread will be registered in default scheduler and ready for
 *     scheduling;
 *   - new thread will have its own memory space unless the 'CLONE_MM' flag
 *     is specified;
 *
 * @param flags - Task creation flags. Possible values are:
 *                CLONE_MM - new task will share its memory space with its
 *                           parent (suitable for creation 'threads').
 * @param attrs - Task creation attributes.
 * @return If new task was successfully created, this function returns
 *         the PID of the new task.
 *         Otherwise, negation of the following error codes is returned:
 *         ENOMEM   No memory was available.
 */
status_t sys_create_task(ulong_t flags,task_creation_attrs_t *a);


/**
 * @fn status_t sys_task_control( pid_t pid, ulong_t cmd, ulong_t arg);
 * @brief Main function for controlling tasks.
 *
 * @param target - Task to control
 * @param cmd - Command. Possible commands are:
 *   SYS_PR_CTL_SET_ENTRYPOINT
 *     Set entrypoint for a newly created task to @a arg. Target task will start
 *     execution from target entrypoint.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_SET_STACK
 *     Set top of user stack for a newly created task to @a arg. Target task will
 *     start execution using this new stack.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_GET_ENTRYPOINT
 *     Get target process's entrypoint.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_GET_STACK
 *     Get top of user stack of target task.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 * @param arg - Command's argument.
 * @return Rreturn values are command-specific.
 * In general, for 'setters', this function returns zero on successful completion,
 * otherwise it returns on of the following errors (negated):
 *    EINVAL - invalid command/argument;
 *    ESRCH - invalid task was specified;
 *    EACCESS - calling process is not allowed to perform the command
 *              requested;
 *    EFAULT - argument points to insufficient address in userspace;
 *    
 */
status_t sys_task_control( pid_t pid, ulong_t cmd, ulong_t arg);

/**
 * @fn status_t sys_mmap(uintptr_t addr,size_t size,uint32_t flags,shm_id_t fd,uintptr_t offset);
 * @brief mmap (shared) memory *
 * @param addr - address where you want to map memory
 * @param size - size of memory to map
 * @param flags - mapping flags
 * @param fd - id of shared memory area
 * @param offset - offset of the shared area to map
 *
 */
status_t sys_mmap(uintptr_t addr,size_t size,uint32_t flags,shm_id_t fd,uintptr_t offset);


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
status_t sys_create_port( ulong_t flags, ulong_t queue_size );

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
status_t sys_port_send(ulong_t channel,
                       uintptr_t snd_buf,ulong_t snd_size,
                       uintptr_t rcv_buf,ulong_t rcv_size);

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
status_t sys_port_receive( ulong_t port, ulong_t flags, ulong_t recv_buf,
                           ulong_t recv_len,port_msg_info_t *msg_info);

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
status_t sys_port_reply(ulong_t port, ulong_t msg_id,ulong_t reply_buf,
                        ulong_t reply_len);

/**
 * @fn status_t sys_allocate_ioports(ulong_t first_port,ulong_t num_ports)
 *
 * Allocate a region of I/O ports.
 * This function reserves target range of I/O ports and makes them accessible
 * for the calling process.
 * @param first_port First I/O port of the range.
 * @param num_ports Number of ports to reserve starting from the @a first_port.
 *
 * @return In case of successful I/O ports allocation this function returns
 * zero. Otherwise, negation of the following errors is returned:
 *   EPERM - Calling process doesn't have enough privileges to access these
 *           I/O ports.
 *   EINVAL - insufficient I/O ports range was provided.
 *   ENOMEM - No free memory to complete the request.
 *   EBUSY - Target I/O ports are already in use.
 */
status_t sys_allocate_ioports(ulong_t first_port,ulong_t num_ports);

/**
 * @fn status_t sys_free_ioports(ulong_t first_port,ulong_t num_ports)
 *
 * Free a region of I/O ports.
 * This function frees target range of I/O ports and makes them unaccessible
 * for the calling process.
 * @param first_port First I/O port of the range.
 * @param num_ports Number of ports to reserve starting from the @a first_port.
 *
 * @return In case of successful I/O ports deallocation this function returns
 * zero. Otherwise, negation of the following errors is returned:
 *   EPERM - Calling process doesn't have enough privileges to access these
 *           I/O ports.
 *   EINVAL - insufficient I/O ports range was provided.
 *   EACCES - Target I/O ports don't belong to the calling process.
 */
status_t sys_free_ioports(ulong_t first_port,ulong_t num_ports);

/**
 * @fn status_t sys_create_irq_counter_array(ulong_t irq_array,ulong_t irqs,
 *                                           ulong_t addr,ulong_t flags)
 * @brief Create a shared memory object for delivering hardware interrupts
 *        to userspace.
 * 
 * This function creates a so-called 'IRQ array' whcih is used for delivering
 * hardware interrupts (IRQs) to userspace.
 * Such arrays consist of two main parts: events bitmask and IRQ counters.
 * a) event bitmask is used as flags to notify that one or more IRQs have
 *    arrived. Application should than read and zeroize this mask atomically
 *    to indocate that it has seen events.
 * b) IRQ counters are used for counting target IRQs. Kernel-level logic only
 *    increments these counters whereas user-space application only decrements
 *    these counters.
 *    For example, if case user wants to monitor two interrupts: 10 and 11,
 *    an IRQ array containing 2 counters will be created so that 0th counter
 *    tracks 10th IRQ and 1st counter tracks 11th IRQ.
 *
 * The structure of IRQ array is as follows:
 *  struct __irq_array {
 *     irq_event_mask_t event_mask;
 *     irq_counter_t counters[];
 *  };
 *
 * @param irq_array Array that contains all IRQ numbers to be monitored.
 * @param irqs Number of elements in @a irq_array (i.e. number of interrupts
 *        being watched).
 * @param addr Page-aligned valid address in userspace for placing the array.
 * @param flags Flags that control behavior of the array.
 *
 * @return In case of successful completion this function returns a non-negative
 * identificator of the new array. Otherwise, a negation of the following errors
 * is returned:
 *      EINVAL  Base address, or number of irqs was zero on invalid, or address
 *              wasn't page-aligned.
 *      EFAULT  Address wasn't a valid user address.
 *      EBUSY   One of interrupts being watched is already being watched by
 *              another process.
 *      ENOMEM  No free memory for internal kernel structures.
 */
status_t sys_create_irq_counter_array(ulong_t irq_array,ulong_t irqs,
                                      ulong_t addr,ulong_t flags);

/**
 * @fn status_t sys_wait_on_irq_array(ulong_t id)
 * Wait for one of target IRQs to arrive.
 *
 * This function checks event mask for target IRQ array and suspends
 * the calling process until one of interrupts being tracked arrives.
 * After resuming calling process target IRQ array's event mask will
 * reflect arrived IRQs and their counters will be incremented to
 * reflect the amount of IRQs arrived.
 *
 * @param ID of the IRQ array being used.
 * @return After occuring one or more IRQs, this function returns zero.
 *         If insufficient buffer ID was used, -EINVAL is returned.
 */
status_t sys_wait_on_irq_array(ulong_t id);

/**
 * @fn status_t sys_ipc_port_poll(pollfd_t *pfds,ulong_t nfds,timeval_t *timeout)
 * Performs I/O multiplexing on a given range of IPC ports.
 *
 * This function checks every port in a range for one of requested events
 * to occur and puts the calling process into sleep util one of target
 * events occur.
 * The following event types are used:
 *    POLLIN - data other than high-priority data may be read without blocking.
 *    POLLRDNORM - normal data may be read without blocking.
 * NOTE: Since IPC ports always block on send (i.e. 'write'), it is impossible
 * to wait for an IPC port to become available for non-blocking write.
 *
 * @param pfds - array of structures that specify desired ports and events.
 * @param nfds - number of structures in the array.
 * @param timeout - operation timeout.
 *        NOTE: Currently timeouts are not supported.
 */
status_t sys_ipc_port_poll(pollfd_t *pfds,ulong_t nfds,timeval_t *timeout);

/**
 * @fn status_t sys_nanosleep(timeval_t *in,timeval_t *out)
 *
 * Suspends calling process for amount of time spicified by the @a in argument.
 * Minimum time unit used is nanosecond.
 * @return After waiting for specified amount of time this function returns zero.
 *   The folowing errors may also be returned:
 *     EFAULT - insufficient address was passed.
 *     EINVAL - seconds or nanoseconds exceed 1000000000.
 */

status_t sys_nanosleep(timeval_t *in,timeval_t *out);

status_t sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg);

status_t sys_get_tid(void);

void sys_exit(int code);

status_t sys_open_channel(pid_t pid,ulong_t port,ulong_t flags);

status_t sys_close_channel(ulong_t channel);

status_t sys_close_port(ulong_t port);

status_t sys_control_channel(ulong_t channel,ulong_t cmd,ulong_t arg);

status_t sys_port_send_iov(ulong_t channel,
                           iovec_t iov[],ulong_t numvecs,
                           uintptr_t rcv_buf,ulong_t rcv_size);

status_t sys_sync_create_object(sync_object_type_t obj_type,
                                void *uobj,uint8_t *attrs,
                                ulong_t flags);

status_t sys_sync_control(sync_id_t id,ulong_t cmd,ulong_t arg);

status_t sys_port_send_iov_v(ulong_t channel,
                             iovec_t snd_iov[],ulong_t snd_numvecs,
                             iovec_t rcv_iov[],ulong_t rcv_numvecs);

status_t sys_port_reply_iov(ulong_t port,ulong_t msg_id,
                            iovec_t reply_iov[],ulong_t numvecs);

#endif

#endif
