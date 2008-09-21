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
 * include/eza/interrupt.h: contains main kernel types and prototypes for dealing
 *                          with hardware interrupts.
 *
 */

#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__ 

#include <ds/list.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/types.h>

/* Structure that describes abstract hardware interrupt controller.
 */
typedef struct __hw_interrupt_controller {
  list_node_t l; /*head of list*/
  const char *descr; /*symboluc name*/
  bool (*handles_irq)( uint32_t irq ); /*Can this PIC use this irq*/
  void (*enable_all)(void); /*globally enable/dissble all irqs*/
  void (*disable_all)(void); 
  void (*enable_irq)( uint32_t irq ); /*enable irq #n*/
  void (*disable_irq)( uint32_t irq ); /*disable irq #n*/
  void (*ack_irq)(uint32_t irq); /*CPU has got interrupt*/
} hw_interrupt_controller_t;

typedef struct __irq_line {
  list_head_t actions;
  hw_interrupt_controller_t *controller;
  uint32_t flags;
} irq_line_t;


typedef void (*irq_handler_t)( void *priv );
typedef uint64_t irq_t;

typedef struct __irq_action {
  irq_handler_t handler;
  uint32_t flags;
  void *private_data;
  list_node_t l;
} irq_action_t;

/* Low-level IRQ entrypoints. */
extern uintptr_t irq_entrypoints_array[NUM_IRQS];

void initialize_irqs( void );
int register_irq(irq_t irq, irq_handler_t handler, void *data, uint32_t flags);
int unregister_irq(irq_t irq, void *data);
int enable_hw_irq(irq_t irq);
int disable_hw_irq(irq_t irq);
void disable_all_irqs(void);
void enable_all_irqs(void);
void do_irq(irq_t irq);

void register_hw_interrupt_controller(hw_interrupt_controller_t *ctrl);

/* Arch-specific routines. */
void arch_initialize_irqs(void);

#define lock_local_interrupts()
#define unlock_local_interrupts()

#define local_interrupts_locked() false

#endif

