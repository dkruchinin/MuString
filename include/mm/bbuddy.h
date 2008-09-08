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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/mm/bbuddy.h: binary buddy abstraction implementation
 *                      part of MuiString mm
 *
 */

#ifndef __MM_BBUDDY_H__
#define __MM_BBUDDY_H__

#include <eza/arch/types.h>

/* typical errors */
#define ENOBLOCK        65535
#define EBUDDYCORRUPED  65534
#define EINVALIDINDX    65533

typedef struct __bbuddy_type {
  uint32_t *pbmp;
  uint32_t *nbmp;
  uint32_t pn;
} bbuddy_t;

/* macro for calculate size per buddy */
#define bbuddy_size(n)  ((((!(n >> 5)) ? 1 : (((n >> 5)>1) ? (n>>5)+2 : (n>>5)+1))*(2*sizeof(uint32_t)))+ \
			 sizeof(bbuddy_t))

/* functions */
extern uint8_t bbuddy_init(bbuddy_t *b,uint32_t max_part);
extern uint32_t bbuddy_block_alloc(bbuddy_t *b, uint32_t align);
extern uint32_t bbuddy_block_release(bbuddy_t *b,uint32_t num);

#endif /* __MM_BBUDDY_H__*/

