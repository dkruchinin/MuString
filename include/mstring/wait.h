#ifndef __WAIT_H__
#define __WAIT_H__

#include <arch/types.h>

typedef unsigned long id_t;

/*
 * status for wait is constructed by the followed way:
 * the first byte - task state change reason
 * (wait_status_t enumeration) with the optional coredump
 * marker, the second byte - exit code or the signal code
 * forced the state change, the third byte - trap event
 * for ptracer.
 */

typedef enum __wait_status {
  WSTAT_EXITED = 0x01,
  WSTAT_STOPPED = 0x02,
  WSTAT_CONTINUED = 0x04,
  WSTAT_SIGNALED = 0x08,
  WSTAT_COREDUMP = 0x10,
} wait_status_t;

#define WNOHANG  0x1
#define WEXITED  0x2
#define WSTOPPED 0x4
#define WCONTINUED 0x8
#define WNOWAIT 0x10

#define WUNTRACED WSTOPPED

/* A special value returned to jointees in case target thread was cancelled. */
#define PTHREAD_CANCELED  ~(ulong_t)0

typedef enum __idtype {
  P_PID=0,   /* Wait for the child whose process ID matches. */
  P_PGID=1,  /* Wait for any child whose process group ID matches. */
  P_ALL=2,   /* Wait for any child.  */
} idtype_t;

#endif
