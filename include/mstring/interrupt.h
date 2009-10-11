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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 * include/mstring/interrupt.h: contains main kernel types and prototypes for dealing
 *                          with hardware interrupts.
 *
 */

#ifndef __MSTRING_INTERRUPT_H__
#define __MSTRING_INTERRUPT_H__ 

#include <arch/interrupt.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <mstring/types.h>

#define NUM_IRQ_LINES IRQ_VECTORS
#define IRQ_INVAL ~(0U)

/**
 * Structure that describes abstract hardware interrupt controller.
 */
struct irq_controller {
  char *name;
  list_node_t ctl_node;
  bool (*can_handle_irq)(irq_t irq);
  void (*mask_all)(void);
  void (*unmask_all)(void); 
  void (*mask_irq)(irq_t irq);
  void (*unmask_irq)(irq_t irq );
  void (*ack_irq)(irq_t irq);
};

typedef uint8_t irqline_flags_t;
#define IRQLINE_ACTIVE 0x01

struct irq_line {
  struct irq_controller *irqctl;
  list_head_t actions;
  uint_t num_actions;
  irq_t irq;
  spinlock_t irq_line_lock;
  struct {
    ulong_t num_irqs;
    uint_t num_sp_irqs;
  } stat;

  irqline_flags_t flags;
};

typedef void (*irq_handler_fn)(void *data);

typedef struct irq_action {
  char *name;
  irq_handler_fn handler;
  list_node_t node;
  irq_t irq;
  void *priv_data;  
} irq_action_t;

extern struct irq_controller *default_irqctrl;

INITCODE void irqs_init(void);
INITCODE void irq_register_controller(struct irq_controller *irqctl);
int irq_line_register(irq_t irq, struct irq_controller *controller);
int irq_line_unregister(irq_t irq);
int irq_register_action(irq_t irq, struct irq_action *action);
int irq_unregister_action(struct irq_action *action);
int irq_mask(irq_t irq);
void irq_mask_all(void);
int irq_unmask(irq_t irq);
void irq_unmask_all(void);
extern void __do_handle_irq(irq_t irq);

#endif /* !__MSTRING_INTERRUPT_H__ */

