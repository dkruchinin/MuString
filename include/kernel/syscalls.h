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
 * (c) Copyright 2010 Jari OS non-profit org <http://jarios.org>
 */

#ifndef __K_SYSCALLS_H__
#define __K_SYSCALLS_H__

#ifndef __ASM__

#include <arch/types.h>
#include <ipc/port.h>
#include <mstring/task.h>
#include <ipc/port.h>
#include <mstring/time.h>
#include <ipc/poll.h>
#include <ipc/ipc.h>
#include <mstring/sync.h>
#include <mstring/signal.h>
#include <mstring/kcontrol.h>
#include <mstring/namespace.h>

#endif

/* Syscalls identificators. */
#define SC_CREATE_TASK         0
#define SC_TASK_CONTROL        1
#define SC_MMAP                2
#define SC_CREATE_PORT         3
#define SC_PORT_RECEIVE        4

#define SC_ALLOCATE_IOPORTS    5
#define SC_FREE_IOPORTS        6
#define SC_CREATE_IRQ_ARRAY    7
#define SC_WAIT_ON_IRQ_ARRAY   8
#define SC_REGISTER_FREE_IRQ   9

#define SC_UNREGISTER_IRQ      10
#define SC_IPC_PORT_POLL       11
#define SC_NANOSLEEP           12
#define SC_SCHED_CONTROL       13
#define SC_EXIT                14

#define SC_OPEN_CHANNEL        15
#define SC_CLOSE_CHANNEL       16
#define SC_CLOSE_PORT          17
#define SC_CONTROL_CHANNEL     18
#define SC_SYNC_CREATE         19

#define SC_SYNC_CONTROL        20
#define SC_SYNC_DESTROY        21
#define SC_KILL                22
#define SC_SIGNAL              23
#define SC_SIGRETURN           24

#define SC_PORT_SEND_IOV_V     25
#define SC_PORT_REPLY_IOV      26
#define SC_SIGACTION           27
#define SC_THREAD_KILL         28
#define SC_SIGPROCMASK         29

#define SC_THREAD_EXIT         30
#define SC_TIMER_CREATE        31
#define SC_TIMER_CONTROL       32
#define SC_MUNMAP              33
#define SC_THREAD_WAIT         34

#define SC_PORT_MSG_READ       35
#define SC_KERNEL_CONTROL      36
#define SC_TIMER_DELETE        37
#define SC_SIGWAITINFO         38
#define SC_SCHED_YIELD         39

#define SC_MEMOBJ_CREATE       40
#define SC_FORK                41
#define SC_GRANT_PAGES         42
#define SC_WAITPID             43
#define SC_ALLOC_DMA           44

#define SC_FREE_DMA            45
#define SC_PORT_CONTROL        46
#define SC_PORT_MSG_WRITE      47
#define SC_PTRACE              48

#define SC_CHG_CREATE_NAMESPACE  49
#define SC_CONTROL_NAMESPACE     50

#define SC_MEMSYNC  51

#define SC_SET_TASK_LIMITS    52
#define SC_GET_TASK_LIMITS    53

#ifndef __ASM__
typedef uint32_t shm_id_t; /* FIXME: remove after merging */

/**
 * @fn status_t sys_get_pid(void)
 * @return PID of the calling process.
 */
long sys_get_pid(void);


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
long sys_create_task(ulong_t flags,task_creation_attrs_t *a);


/**
 * @fn status_t sys_task_control( pid_t pid, ulong_t cmd, ulong_t arg);
 * @brief Main function for controlling tasks.
 *
 * @param target - Task to control (PID)
 * @param tid    - Thread within the target process (or zero if the main
 *                 thread is assumed)
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
long sys_task_control( pid_t pid, tid_t tid, ulong_t cmd, ulong_t arg);



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
int sys_allocate_ioports(ulong_t first_port,ulong_t num_ports);

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
int sys_free_ioports(ulong_t first_port,ulong_t num_ports);

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
long sys_create_irq_counter_array(ulong_t irq_array,ulong_t irqs,
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
int sys_wait_on_irq_array(ulong_t id);

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
 *
 * @return This function returns number of events occured on target ports,
 *         zero if timeout has elapsed, or a negation of error:
 *         EINVAL - insufficient @a pfds or @a nfds passed.
 *         EFAULT - @pfds pointed to insufficient memory area.
 */
long sys_ipc_port_poll(pollfd_t *pfds,ulong_t nfds,timeval_t *timeout);

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
int sys_nanosleep(timeval_t *in,timeval_t *out);

/**
 * @fn status_t sys_scheduler_control(pid_t pid,tid_t tid,ulong_t cmd,ulong_t arg)
 *
 * Read or change a parameter related to scheduling scheme of target task.
 *
 * @param pid - Target task (PID).
 * @param tid - Target task (TID).
 * @param cmd - Command to apply.
 * @param arg - Command's argument.
 *
 * @return In case of successful completion this function returns current value of
 * target scheduling parameter in case of 'get' operation (usually it is above zero
 * or zero), or a negation of the following errors:
 *     EINVAL - insuffucient command or argument provided.
 *     EFAULT - insufficient memory location was used as an argument for the command.
 *     EPERM - operation was not allowed.
 *     ESRCH - insufficient pid passed.
 */
long sys_scheduler_control(pid_t pid, tid_t tid, ulong_t cmd, ulong_t arg);

/**
 * @fn status_t sys_get_tid(void)
 *
 * Get thread identificator of current task.
 *
 * @return If the calling task is a process root task, its TID (that is equal
 * to its PID) is returned. In case of a thread, a value greater than MAX_PID
 * is returned as its thread ID.
 */
long sys_get_tid(void);

/**
 * @fn void sys_exit(int code)
 *
 * Terminates the calling task and frees all its resources.
 * @return This function doesn't return.
 */
void sys_exit(int code);


long sys_sync_create_object(sync_object_type_t obj_type,
                                void *uobj,uint8_t *attrs,
                                ulong_t flags);

long sys_sync_control(sync_id_t id,ulong_t cmd,ulong_t arg);


long sys_thread_kill(pid_t prcess,tid_t tid,int sig);

long sys_sigprocmask(int how,sigset_t *set,sigset_t *oldset);

long sys_thread_wait(tid_t tid,void **value_ptr);

long sys_kernel_control(kcontrol_args_t *arg);

long sys_timer_delete(long id);

long sys_sigwaitinfo(sigset_t *set,int *sig,usiginfo_t *info,
                     timespec_t *timeout);

void sys_sched_yield(void);

long sys_waitpid(pid_t pid,int *status,int options);

long sys_port_msg_write(ulong_t port, ulong_t msg_id, iovec_t *iovecs,
                        ulong_t numvecs,off_t offset);

#endif

#endif
