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
 * include/eza/amd64/e820map.h: constant used in bootstrap memory mapping
 *                              struct for e820 mapping
 *
 */


#ifndef __E820MAP_H__
#define __E820MAP_H__

#define E820MAP_MEMORY_AVAILABLE  1 /* ok, we're can use it */
#define E820MAP_MEMORY_RESERVED   2 /* reserved by bios */
#define E820MAP_MEMORY_ACPI       3 /* acpi accessible area*/
#define E820MAP_MEMORY_NVS        4 /* don't in use, needed for rest/sav on nvs sleep */
#define E820MAP_MEMORY_UNUSABLE   5 /* doesn't accessible area*/

#define E820MAP_E820_RECORDSIZE   20 /* size of entry within e820 */
#define E820MAP_E820_MAXRECORDS   32 /* entries in e820 */

#ifndef __ASM__ 

#include <eza/arch/types.h>

typedef enum __e820_memory_types {
  E820_USABLE = 1,
  E820_RESERVED = 2,
  E820_ACPI_RECLAIMABLE = 3,
  E820_ACPI_NON_VOLATILE = 4,
  E820_BAD = 5,
} e820_memory_types_t;

typedef struct {
  uint64_t base_address;
  uint32_t length_low, length_high;
  uint32_t type;
} __attribute__ ((packed)) e820memmap_t;

extern e820memmap_t e820table[E820MAP_E820_MAXRECORDS];
extern uint8_t e820count;

#endif /* __ASM__ */

#endif /* __E820MAP_H__ */

