#ifndef __WAIT_H__
#define __WAIT_H__

#include <eza/arch/types.h>

typedef unsigned long id_t;

#define WNOHANG  0x1

/* A special value returned to jointees in case target thread was cancelled. */
#define PTHREAD_CANCELED  ~(ulong_t)0

typedef enum __idtype {
  P_PID=0,   /* Wait for the child whose process ID matches. */
  P_PGID=1,  /* Wait for any child whose process group ID matches. */
  P_ALL=2,   /* Wait for any child.  */
} idtype_t;

#endif
