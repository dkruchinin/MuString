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
 * include/eza/amd64/ioapic.h: implements IO APIC support driver.
 *
 */

#ifndef __ARCH_IOAPIC_H__
#define __ARCH_IOAPIC_H__

#include <arch/types.h>

#define DEFAULT_IOAPIC_ADDR  0xFEC00000
#define IOAPIC_NUM_PAGES 1
#define MAX_IOAPICS 8

/* here the black magic defines */
#define IOAPICRG   (0x00/sizeof(uint32_t)) /* IO APIC register selector offset */
#define IOAPICWIN  (0x10/sizeof(uint32_t)) /* IO APIC window for input/output offset */
#define IOAPICRED  0x10 /* IO APIC redirection table address */
/* delivery modes (TX) */
#define TXMODE_FIXED	0x0
#define TXMODE_LOWPRI	0x1
#define TXMODE_SMI	0x2
#define TXMODE_NMI	0x4
#define TXMODE_INIT	0x5
#define TXMODE_STARTUP	0x6
#define TXMODE_EXTINT	0x7
/* destination modes */
#define DMODE_LOGIC  0x1
#define DMODE_PHY    0x0
/* trigger modes */
#define TRIG_EDGE   0x0
#define TRIG_LEVEL  0x1

#define LOW_PRIORITY  (1 << 0)

/* registers addresses */
#define IOAPIC_IDREG   0x0
#define IOAPIC_VERREG  0x1

/* level modes*/
#define LEVEL_DEASSERT  0x1
#define LEVEL_ASSERT    0x0

/* short hand modes */
#define SHORTHAND_NIL       0x0 /* none nobody */
#define SHORTHAND_SELF      0x1 /* self only */
#define SHORTHAND_ALLABS    0x2 /* absolutely all */
#define SHORTHAND_ALLEXS    0x3 /* all exclude ipi */

typedef union {
  uint32_t value;
  struct {
    uint32_t __res0 : 24,
    id : 4, /* io apic ID, must be unique to operate */
    __res1 : 4;
  } __attribute__ ((packed));
} ioapic_id_t;

typedef union {
  uint32_t value;
  struct {
    uint32_t version : 8,
    __res0 : 8,
    redir_entries : 8, /* maximum redirection entries */
    __res1 : 8;
  } __attribute__ ((packed));
} ioapic_version_t;

typedef union {
  uint32_t value;
  struct {
    uint8_t addr; /* apic register address*/
    uint32_t __res0 : 24;
  } __attribute__ ((packed));
} ioapic_regsel_t; /* register selector */

typedef struct __io_apic_redir_type {
  union {
    uint32_t low;
    struct {
      uint8_t vector; /* interrupt vector */
      uint32_t txmod : 3, /* delivery mode*/
      dmod : 1, /* destination mode */
      txstate : 1, /* delivery status */
      polarity : 1, /* polarity */
      irr : 1, /* RO - remote irr*/
      trig_mode : 1, /* trigger mode (edge|level) */
      mask : 1, /* masking */
      __res0 : 15; /* reserved */
    } __attribute__ ((packed));
  };
  union {
    uint32_t high;
    struct {
      uint32_t __res1 : 24, /* like usually in apic and io apic - reserved */
      dest : 8; /* destination area */
    } __attribute__ ((packed));
  };
} __attribute__ ((packed)) ioapic_redir_t;

void save_next_ioapic_info(uint32_t *base_va, irq_t base_irq);
void ioapic_init();

#endif /* __ARCH_IOAPIC_H__ */
