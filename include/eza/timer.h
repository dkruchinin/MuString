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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 *
 * include/eza/timer.h: contains main kernel types and prototypes for dealing
 *                      with hardware timers.
 *
 */

#ifndef __EZA_TIMER_H__
#define __EZA_TIMER_H__

#include <ds/list.h>
#include <mlibc/types.h>
#include <eza/interrupt.h>

typedef struct __hw_timer_type {
  list_node_t l;
  const char *descr;
  void (*calibrate)(uint32_t hz);
  void (*resume)(void);
  void (*suspend)(void);
  void (*register_callback)(irq_t irq,irq_handler_t handler);
} hw_timer_t;

void hw_timer_register(hw_timer_t *ctrl);

typedef void (*timer_handler_t)(ulong_t data);

#define TF_TIMER_ACTIVE  0x1        /* Timer is active and ticking. */

typedef struct __timer {
  list_node_t l;
  ulong_t time_x,flags,data;
  timer_handler_t handler;
} timer_t;

void init_timers(void);
void init_timer(timer_t *t);
bool add_timer(timer_t *t);
void delete_timer(timer_t *t);
void adjust_timer(timer_t *t,long delta);
void process_timers(void);

#endif /*__EZA_TIMER_H__*/

