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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 */

#ifndef __MSTRING_SPINLOCK_UP_H__
#define __MSTRING_SPINLOCK_UP_H__

#define __spin_lock(spin)    UNUSED(spin)
#define __spin_trylock(spin) ({UNUSED(spin); true;})
#define __spin_unlock(spin)  UNUSED(spin)

#define __rw_lock_read(rwl)    UNUSED(rwl)
#define __rw_lock_write(rwl)   UNUSED(rwl)
#define __rw_unlock_read(rwl)  UNUSED(rwl)
#define __rw_unlock_write(rwl) UNUSED(rwl)

#define __spin_lock_bit(bitmap, bit)            \
  do { UNUSED(bitmap); UNUSED(bit); } while (0)
#define __spin_unlock_bit(bitmap, bit)          \
  do { UNUSED(bitmap); UNUSED(bit); } while (0)

#define __bound_spin_lock(bspin, cpu)           \
  do { UNUSED(bspin); UNUSED(cpu); } while (0)
#define __bound_spin_trylock(bspin, cpu)        \
  ({ UNUSED(bspin); UNUSED(cpu); true; })
#define __bound_spin_unlock(bspin) UNUSED(bspin)

#endif /* !__MSTRING_SPINLOCK_UP_H__ */
