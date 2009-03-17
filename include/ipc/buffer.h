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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/ipc/buffer.h: prototypes and data types for IPC buffers.
 *
 */

#ifndef __IPC_BUFFER__
#define  __IPC_BUFFER__

#include <mm/page.h>
#include <eza/arch/types.h>
#include <eza/arch/atomic.h>

typedef struct __ipc_buffer {
  page_idx_t *chunks;
  size_t length;
  uint32_t offset;
  uint32_t num_chunks;
} ipc_buffer_t;

struct __iovec;
int ipc_setup_buffer_pages(struct __iovec *iovecs, uint32_t numvecs,
                           page_idx_t *idx_array, ipc_buffer_t *bufs, bool is_sender_buffer);
int ipc_transfer_buffer_data_iov(ipc_buffer_t *bufs, uint32_t numbufs,
                                 struct __iovec *iovecs, uint32_t numvecs,
                                 ulong_t offset, bool to_buffer);
#endif
