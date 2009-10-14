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
 * (c) Copytight 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#ifndef __MSTRING_ARCH_APIC_H__
#define __MSTRING_ARCH_APIC_H__

#include <config.h>
#include <mm/page.h>
#include <mstring/smp.h>
#include <mstring/types.h>

#define LAPIC_NUM_PAGES    (PAGE_ALIGN(4096) >> PAGE_WIDTH)
#define DEFAULT_LAPIC_ADDR 0xFEE00000

enum {
  APIC_ID           = 0x020, /* Local APIC ID */
  APIC_VERSION      = 0x030, /* Local APIC version register */
  APIC_TPR          = 0x080, /* Task Priority Register */
  APIC_APR          = 0x090, /* Arbitration Priority Register */
  APIC_PPR          = 0x0A0, /* Processor Priority Register */
  APIC_EOIR         = 0x0B0, /* End Of Interrupt Register */
  APIC_RRR          = 0x0C0, /* Remote Read Register */
  APIC_LDR          = 0x0D0, /* Logical Destination Register */
  APIC_DFR          = 0x0E0, /* Destination Format Register */
  APIC_SIVR         = 0x0F0, /* Spurious Interrupt Vector Register */
  APIC_ISR_BASE     = 0x100, /* In-Service Registers(ISRs) 0x100 - 0x170 */

  /* 0x180 - 0x1F0 - Trigger mode registers(TMRs) */
  /* 0x200 - 0x270 - Interrupt Request reisters(IRRs) */

  APIC_ESR          = 0x280, /* Error status register */
  APIC_ICR_LOW      = 0x300, /* Interrupt command register(low 32 bits) */
  APIC_ICR_HIGH     = 0x310, /* Interrupt command register(high 32 bits) */
  APIC_TIMER_LVTE   = 0x320, /* Timer Local vector table entry */
  APIC_THERMAL_LVTE = 0x330, /* Thermal local vectory table entry */
  APIC_PC_LVTE      = 0x340, /* Performance caounter LVT entry */
  APIC_LI0_VTE      = 0x350, /* Local interrupt 0 vector table entry */
  APIC_LI1_VTE      = 0x360, /* Local interrupt 1 vector table entry */
  APIC_ERROR_LVTE   = 0x370, /* Error vector table entry */
  APIC_TIMER_ICR    = 0x380, /* Timer initial count register */
  APIC_TIMER_CCR    = 0x390, /* Timer current count register */
  APIC_TIMER_DCR    = 0x3E0, /* Timer divide configuration register */
  APIC_EFR          = 0x400, /* Extended APIC feature register */
  APIC_ECR          = 0x410, /* Extended APIC control register */
  APIC_SEOI         = 0x420, /* Specific end-of-interrupt register */
  APIC_IER_BASE     = 0x480, /* Interrupt enable regsiters base(0x480 - 0x4F0) */
};

#define APIC_VECTOR_MASK 0xFFU

/* APIC Destination formats */
#define APIC_DFR_FLAT    0xFFFFFFFFU
#define APIC_DFR_CLUSTER 0x0FFFFFFFU
#define SET_APIC_LOGID(id) ((id) << 24)

/* Number of ISRs on local APIC */
#define APIC_NUM_ISRS 8

/* Spurious interrupt register related stuff */
#define APIC_SVR_MASK     0x30FU
#define APIC_SVR_ENABLED   (1 << 8)
#define APIC_FP_DISABLED   (1 << 9)

#define APIC_LVTDM_MASK      0x700
#define APIC_LVTDS_MASK      0x1000

/* APIC LVT delivery modes */
#define APIC_LVTDM_FIXED     0
#define APIC_LVTDM_SMI       0x200
#define APIC_LVTDM_NMI       0x400
#define APIC_LVTDM_INIT      0x500
#define APIC_LVTDM_STARTUP   0x600
#define APIC_LVTDM_EXTINT    0x700

/* LVT level */
#define APIC_LVTL_ASSERT 0x4000

/* APIC LVT delivery modes */
#define APIC_LVTDS_IDLE         0
#define APIC_LVTDS_SEND_PENDING 0x1000

/* Other APIC LVT stuff */
#define APIC_LVT_IIPP          0x02000 /* Interrupt input pin polarity */
#define APIC_LVT_RIRRF         0x04000 /* Remote IRR flag */
#define APIC_LVT_TRIGGER_MODE  0x08000 /* Trigger mode  */
#define APIC_LVT_MASKED        0x10000 /* Interrupt is masked */
#define APIC_LVT_TIMER_MODE    0x20000 /* Timer mode */

#define APIC_SET_TIMER_BASE(x) ((x) << 18)
#define APIC_GET_TIMER_BASE(x) (((x) >> 18) & 0x03)
#define APIC_TDR_DIV_TMBASE 0x04

/* Local APIC timer divide values */
#define APIC_DV2   0x00
#define APIC_DV4   0x01
#define APIC_DV8   0x02
#define APIC_DV16  0x03
#define APIC_DV32  0x04
#define APIC_DV64  0x05
#define APIC_DV128 0x06
#define APIC_DV1   0x07

/* APIC LDR stuff */
#define APIC_LOGID_SHIFT 24
#define APIC_LDR_MASK    (0xFFU << APIC_LOGID_SHIFT)

/* APIC Error codes */
#define APIC_ERR_SAE  0x04 /* Sent Accept Error */
#define APIC_ERR_RAE  0x08 /* Receive Accept Error */
#define APIC_ERR_SIV  0x20 /* Sent Illegal Vector */
#define APIC_ERR_RIV  0x40 /* Receive illegal vector */
#define APIC_ERR_IRA  0x80 /* Illegal register address */
#define APIC_ERR_MASK 0xEC

extern volatile uintptr_t lapic_addr;
extern uint32_t PER_CPU_VAR(lapic_ids);

INITCODE void lapic_init(cpu_id_t cpuid);
INITCODE int lapic_init_ipi(uint32_t apic_id);
INITCODE void lapic_timer_init(cpu_id_t cpuid);

#endif /* !__MSTRING_ARCH_APIC_H__ */
