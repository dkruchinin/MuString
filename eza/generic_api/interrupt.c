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
 * eza/generic_api/interrupt.c: contains routines for dealing with hardware
 *                              interrupts. 
 *
 */
/* Changes:
 * added (disable|enable)_hw_irq() functions
 *  - Tirra
 */

#include <ds/list.h>
#include <eza/interrupt.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <eza/smp.h>
#include <eza/arch/interrupt.h> /* Arch-specific constants. */
#include <mlibc/string.h>
#include <mlibc/kprintf.h>
#include <eza/swks.h>
#include <eza/arch/preempt.h>

static spinlock_declare(irq_lock);

static LIST_DEFINE(known_hw_int_controllers);
static irq_line_t irqs[NUM_IRQS];

#define GRAB_IRQ_LOCK() spinlock_lock(&irq_lock)
#define RELEASE_IRQ_LOCK() spinlock_unlock(&irq_lock)

list_head_t thead;

static irq_action_t *allocate_irq_action( void )
{
  static irq_action_t descs[512];
  static int idx = 5;
  static irq_action_t *desc;

  desc = &descs[idx];
  list_init_node( &desc->l );
  idx++;
  return desc;
}

static void install_irq_action( uint32_t irq, irq_action_t *desc ) 
{
  list_add2tail( &irqs[irq].actions, &desc->l);
}

static void remove_irq_action( irq_action_t *desc ) {
  list_del( &desc->l );
}

void register_hw_interrupt_controller(hw_interrupt_controller_t *ctrl)
{
  int idx;

  GRAB_IRQ_LOCK();
  list_add2tail(&known_hw_int_controllers, &ctrl->l);

  for( idx = 0; idx < NUM_IRQS; idx++ ) {
    if( irqs[idx].controller == NULL ) {
      if( ctrl->handles_irq(idx) ) {
        irqs[idx].controller = ctrl;
      }
    }
  }
  RELEASE_IRQ_LOCK();
}

int disable_hw_irq(irq_t irq)
{
  uint32_t r=0;

  if(irq>=NUM_IRQS)
    return -EINVAL;

  GRAB_IRQ_LOCK();
  if(irqs[irq].controller)
    irqs[irq].controller->disable_irq(irq);
  else r=-EINVAL;

  RELEASE_IRQ_LOCK();

  return r;
}

int enable_hw_irq(irq_t irq)
{
  uint32_t r=0;

  if(irq>=NUM_IRQS)
    return -EINVAL;

  GRAB_IRQ_LOCK();
  if(irqs[irq].controller)
    irqs[irq].controller->enable_irq(irq);
  else r=-EINVAL;

  RELEASE_IRQ_LOCK();

  return r;
}

int register_irq(irq_t irq, irq_handler_t handler, void *data, uint32_t flags)
{
  int retval = -EINVAL;

  if( irq < 256 && handler != NULL ) {
    irq_action_t *desc = allocate_irq_action();

    GRAB_IRQ_LOCK();
  
    desc->handler = handler;
    desc->flags = flags;
    desc->private_data = data;

    install_irq_action(irq,desc);

    /* Enable IRQ line for this interrupt since its handler is now
     * installed.
     */
    irqs[irq].controller->enable_irq(irq);

    RELEASE_IRQ_LOCK();
    retval = 0;
  }
  
  return retval;
}

int unregister_irq(irq_t irq, void *data)
{
  int retval = -EINVAL;
  irq_action_t *desc;

  if( irq < 256 ) {
    GRAB_IRQ_LOCK();
    list_for_each_entry(&irqs[irq].actions, desc, l) {
      if( desc->private_data == data ) {
        remove_irq_action(desc);
      }
    }
    RELEASE_IRQ_LOCK();
  }
  return retval;
}

void initialize_irqs( void )
{
  int i;

  for( i = 0; i < 256; i++ ) {
    list_init_head(&irqs[i].actions);

    irqs[i].controller = NULL;
    irqs[i].flags = 0;
  }

  spinlock_initialize( &irq_lock, "irq_lock" );
}

void disable_all_irqs(void)
{
  hw_interrupt_controller_t *p;

  GRAB_IRQ_LOCK();
  list_for_each_entry(&known_hw_int_controllers, p, l) {
    p->disable_all();
  }
  RELEASE_IRQ_LOCK();
}

void enable_all_irqs(void)
{
  hw_interrupt_controller_t *p;

  GRAB_IRQ_LOCK();
  list_for_each_entry(&known_hw_int_controllers, p, l) {
    p->enable_all();
  }
  RELEASE_IRQ_LOCK();
}


void do_irq(irq_t irq)
{
  if( irq < 256 ) {
    int cpu = cpu_id();

    int handlers = 0;
    cpu_stats_t *cpu_stat = &swks.cpu_stat[cpu];
    irq_action_t *action;

    cpu_stat->irq_stat[irq]++;

    /* Call all handlers. */
    list_for_each_entry(&irqs[irq].actions, action, l) {
      action->handler( action->private_data );
      handlers++;
    }

    /* Ack this interrupt. */
    irqs[irq].controller->ack_irq(irq);

    if( handlers > 0) {
      return;
    }
  }
  interrupts_enable();
  kprintf( "** Spurious interrupt detected: %d\n", irq );
}

