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
 * include/ipc/poll.h: Data types, constants andprototypes for the IPC ports
 *                     polling mechanism.
 *
 */

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
