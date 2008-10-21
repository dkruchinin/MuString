#ifndef __IPC_POLL_H__
#define  __IPC_POLL_H__

#include <eza/arch/types.h>

#define POLLIN        0x1  /* Data may be read without blocking. */
#define POLLRDNORM    0x2  /* Normal data may be read without blocking. */
#define POLLOUT       0x4  /* Data may be written without blocking. */
#define POLLWRNORM    0x8  /* Equivalent to POLLOUT. */

#define MAX_POLL_OBJECTS  65535

typedef uint16_t poll_event_t;

typedef struct __pollfd {
  ulong_t fd;
  poll_event_t events;
  poll_event_t revents;
} pollfd_t;

#endif
