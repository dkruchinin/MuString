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

#ifndef __MSTRING_SPINLOCK_SMP_H__
#define __MSTRING_SPINLOCK_SMP_H__

#define __spin_lock(spin)    arch_spinlock_lock(spin)
#define __spin_trylock(spin) arch_spinlock_trylock(spin)
#define __spin_unlock(spin)  arch_spinlock_unlock(spin)

#define __rw_lock_read(rwlock)                  \
  arch_spinlock_lock_read(rwlock)
#define __rw_lock_write(rwlock)                 \
  arch_spinlock_lock_write(rwlock)
#define __rw_unlock_read(rwlock)                \
  arch_spinlock_unlock_read(rwlock)
#define __rw_unlock_write(rwlock)               \
  arch_spinlock_unlock_write(rwlock)

#define __spin_lock_bit(bitmap, bit)            \
  arch_spinlock_lock_bit(bitmap, bit)
#define __spin_unlock_bit(bitmap, bit)          \
  arch_spinlock_unlock_bit(bitmap, bit)

#define __bound_spin_lock(bspin, cpu)           \
  arch_bound_spinlock_lock_cpu(bspin, cpu)
#define __bound_spin_trylock(bspin, cpu)        \
  arch_bound_spinlock_trylock_cpu(bspin, cpu)
#define __bound_spin_unlock(bspin)              \
  arch_bound_spinlock_unlock_cpu(bspin)

#endif /* !__MSTRING_SPINLOCK_SMP_H__ */
