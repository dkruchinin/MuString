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
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/ipc/port_ctl.h: Data-types for IPC port control.
 *
 */

#ifndef __IPC_PORT_CTL_H__
#define __IPC_PORT_CTL_H__

#include <ipc/ipc.h>

#define IPC_PORT_CTL_DIR_HEAD  0x1

typedef struct __port_ctl_msg_ch_descr {
  ulong_t msg_id,ch_id;
} ipc_port_ctl_msg_ch_descr_t;

typedef struct __port_ctl_msg_extra_data {
  ulong_t msg_id,flags;
  union {
    iovec_t data;
    long code;
  } d;
} port_ctl_msg_extra_data_t;

enum ipc_port_ctrl_command {
  IPC_PORT_CTL_MSG_FWD =    0, /** Forward message over target channel. */
  IPC_PORT_CTL_MSG_APPEND = 1, /** Append extra data to original message */
  IPC_PORT_CTL_MSG_CUT = 2,    /** Cut extra data from message */
  IPC_PORT_CTL_MSG_REPLY_RETCODE = 3, /*Just wake up sender and transfer the retcode. */
};

#endif
