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
 *
 * include/mstring/cap.h - Capabilities API.
 *
 */

#ifndef __CAP_H__
#define __CAP_H__

typedef caps_mask_t uint32_t;

typedef enum __cap {
  CAP_CAN_FORK       = 0x01,
  CAP_CAN_CRT_THREAD = 0x02,
  CAP_CAN_CRT_MEMOBJ = 0x04,
  CAP_CAN_BE_BACKEND = 0x08,
  CAP_CAN_OPEN_PORT  = 0x10,
} cap_t;

#define ROOT_CAP_MASK                                                   \
  (CAP_CAN_FORK | CAP_CAN_CRT_THREAD | CAP_CAN_CRT_MEMOBJ               \
   | CAP_CAN_BE_BACKEND | CAP_CAN_OPEN_PORT)

static inline bool cap_check(caps_mask_t *caps_mask, cap_t cap)
{
  return !!(*caps_mask & cap);
}

static inline void cap_set(cap_mask_t *caps_mask, cap_t cap)
{
  *caps_mask |= cap;
}

static inline void cap_clear(cap_mask_t *caps_mask, cap_t cap)
{
  *caps_mask &= ~cap;
}

#endif /* __CAP_H__ */
