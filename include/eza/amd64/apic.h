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
 * include/eza/amd64/apic.h: implements local APIC support driver.
 *
 */

#ifndef __ARCH_APIC_H__
#define __ARCH_APIC_H__

#include <config.h>
#include <eza/arch/types.h>

#define APIC_BASE    0xfee00000

#define APIC_INT_EOI  0xb0

struct __local_apic_id_t { /* 16 bytes */
  uint32_t __reserved0 : 24,
    phy_apic_id : 4,
    __reserved1 : 4;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_version_t { /* 16 bytes */
  uint32_t version : 8,
    __reserved0 : 8,
    max_lvt : 8,
    __reserved1 : 8;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_tpr_t { /* task priority regoster */
  uint32_t priority : 8,
    __reserved0 : 24;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_apr_t { /* arbitration priority register */
  uint32_t priority : 8,
    __reserved0 : 24;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_ppr_t { /* processor priority register */
  uint32_t priority : 8,
    __reserved0 : 24;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_eoi_t { /* end of interrupt */
  uint32_t eoi;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_ldr_t { /* logical destination register */
  uint32_t __reserved0 : 24,
    log_dest : 8;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_dfr_t { /* destination format register */
  uint32_t __reserved0 : 28,
    mode : 4;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_svr_t { /* spurious interrupt vector register */
  uint32_t spurious_vector : 8,
    apic_enabled : 1,
    cpu_focus : 1,
    __reserved0 : 22;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_isr_t { /* in service register */
  uint32_t bits;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_tmr_t { /* trigger mode register */
  uint32_t bits;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_irr_t { /* interrupt request register */
  uint32_t bits;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_esr_t { /* error status register */
  uint32_t tx_cs_err : 1,
    rx_cs_err : 1,
    tx_accept_err : 1,
    rx_accept_err : 1,
    __res0 : 1,
    tx_illegal_vector : 1,
    rx_illegal_vector : 1,
    reg_illegal_addr : 1,
    __res1 : 24;
  uint32_t __reserved0;
  uint32_t errs;
  uint32_t __reserved1;
} __attribute__ ((packed));

struct __local_apic_icr1_t {
  uint32_t vector : 8,
    tx_mode : 3,
    rx_mode: 1,
    tx_status: 1,
    __res0: 1,
    level: 1,
    trigger: 1,
    __res1: 2,
    shorthand: 2,
    __res2: 12;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_icr2_t {
  uint32_t __res0 : 24,
    phy_dest : 4,
    __res1 :  4;
  uint32_t __res2 : 24,
    logical_dest: 8;
  uint32_t __reserved[2];
} __attribute__ ((packed));

struct __local_apic_lvt_timer_t {
  uint32_t vector : 8,
    __res0 : 4,
    tx_status : 1,
    __res1 : 3,
    mask : 1,
    timer_mode : 1,
    __res2 : 14;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_lvt_thermal_sensor_t {
  uint32_t vector : 8,
    tx_mode : 3,
    __res0 : 1,
    tx_status : 1,
    __res1 : 3,
    mask : 1,
    __res2 : 15;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_lvt_pc_t { /* LVT performance counter */
  uint32_t vector : 8,
    tx_mode : 3,
    __res0 : 1,
    tx_status : 1,
    __res1 : 3,
    mask : 1,
    __res2 : 15;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_lvt_lint_t {
  uint32_t vector : 8,
    tx_mode : 3,
    __res0 : 1,
    tx_status : 1,
    polarity : 1,
    remote_irr : 1,
    trigger : 1,
    mask : 1,
    __res1 : 15;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_lvt_error_t { /* LVT error register */
  uint32_t vector : 8,
    tx_mode: 3,
    __res0 : 1,
    tx_status : 1,
    __res1 : 3,
    mask : 1,
    __res2 : 15;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_timerst_t { /* LVT timers stuff */
  uint32_t count;
  uint32_t __reserved[3];
} __attribute__ ((packed));

struct __local_apic_timer_dcr_t {
  uint32_t divisor : 4, 
    __res0 : 28;
  uint32_t __reserved[3];
} __attribute__ ((packed));

/* I won't use many read/writes */
struct __local_apic_t {
  /* |000->|010 */
  uint32_t __reserved0[4]; /* 16 bytes */
  uint32_t __reserved1[4];
  /* |020 */
  struct __local_apic_id_t id;
  /* |030 */
  const struct __local_apic_version_t version;
  /* |040->|070 */
  uint32_t __reserved2[4]; /* 16 bytes */
  uint32_t __reserved3[4]; /* 16 bytes */
  uint32_t __reserved4[4]; /* 16 bytes */
  uint32_t __reserved5[4]; /* 16 bytes */
  /* |080 */
  struct __local_apic_tpr_t tpr;
  /* |090 */
  const struct __local_apic_apr_t apr;
  /* |0A0 */
  const struct __local_apic_ppr_t ppr;
  /* |0B0 */
  struct __local_apic_eoi_t eoi;
  /* |0C0 */
  uint32_t __reserved6[4]; /* 16 bytes */
  /* |0D0 */
  struct __local_apic_ldr_t ldr;
  /* |0E0 */
  struct __local_apic_dfr_t dfr;
  /* |0F0 */
  struct __local_apic_svr_t svr;
  /* |100->|170 (8 items) */
  struct __local_apic_isr_t isr[8];
  /* |180->|1F0 (8 items) */
  struct __local_apic_tmr_t tmr[8];
  /* |200->|270 (8 items) */
  struct __local_apic_irr_t irr[8];
  /* |280 */
  struct __local_apic_esr_t esr;
  /* |290->|2F0 */
  uint32_t __reserved7[4]; /* 16 bytes */
  uint32_t __reserved8[4]; /* 16 bytes */
  uint32_t __reserved9[4]; /* 16 bytes */
  uint32_t __reserved10[4]; /* 16 bytes */
  uint32_t __reserved11[4]; /* 16 bytes */
  uint32_t __reserved12[4]; /* 16 bytes */
  uint32_t __reserved13[4]; /* 16 bytes */
  /* |300 */
  struct __local_apic_icr1_t icr1; /* interrupt command registers 1-2 */
  /* |310 */
  struct __local_apic_icr2_t icr2;
  /* |320 */
  struct __local_apic_lvt_timer_t lvt_timer;
  /* |330 */
  struct __local_apic_lvt_thermal_sensor_t lvt_thermal_sensor;
  /* |340 */
  struct __local_apic_lvt_pc_t lvt_pc;
  /* |350 */
  struct __local_apic_lvt_lint_t lvt_lint0;
  /* |360 */
  struct __local_apic_lvt_lint_t lvt_lint1;
  /* |370 */
  struct __local_apic_lvt_error_t lvt_error;
  /* |380 */
  struct __local_apic_timerst_t timer_icr; /* LVT timer initial count register */
  /* |390 */
  const struct __local_apic_timerst_t timer_ccr; /* LVT timer current count register */
  /* |3A0->|3D0 */
  uint32_t __reserved14[4]; /* 16 bytes */
  uint32_t __reserved15[4]; /* 16 bytes */
  uint32_t __reserved16[4]; /* 16 bytes */
  uint32_t __reserved17[4]; /* 16 bytes */
  /* |3E0 */
  struct __local_apic_timer_dcr_t timer_dcr; /* timer divider configuration register */
  /* |3F0 */
  uint32_t __reserved18[4]; /* 16 bytes */

} __attribute__ ((packed));

/*uffh, functions*/
void local_bsp_apic_init(void);
void local_apic_bsp_switch(void);

void local_apic_timer_init(void);

#ifdef CONFIG_SMP

void arch_smp_init(void);

#endif /* CONFIG_SMP 
*/
#endif

