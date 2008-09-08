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
 * eza/generic_api/timer.c: contains routines for dealing with hardware
 *                          timers. 
 *
 */

#include <eza/interrupt.h>
#include <eza/list.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <mlibc/string.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <eza/timer.h>

/*spinlock*/
static spinlock_declare(timer_lock);
/*list of the timers*/
static LIST_HEAD(known_hw_timers);

#define GRAB_TIMER_LOCK() spinlock_lock(&timer_lock)
#define RELEASE_TIMER_LOCK() spinlock_unlock(&timer_lock)

void init_hw_timers (void)
{
  init_list_head(&known_hw_timers);
}

void hw_timer_register (hw_timer_t *ctrl)
{
  int idx;

  GRAB_TIMER_LOCK();
  list_add(&ctrl->l, &known_hw_timers);

  RELEASE_TIMER_LOCK();
}

void hw_timer_generic_suspend(void)
{

}
