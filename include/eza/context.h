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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/context.h: wrappers for arch depended functions
 *                        setcontext macros
 *
 */

#ifndef __EZA_CONTEXT_H__
#define __EZA_CONTEXT_H__

#include <eza/arch/types.h>
#include <eza/arch/context.h>

extern int arch_context_save(context_t *c);
extern void arch_context_restore(context_t *c) __attribute__ ((noreturn));

static inline void context_restore(context_t *c)
{
  arch_context_restore(c);
}

#define context_set(c,__pc,stack,size)  (c)->pc=(uintptr_t) (__pc); \
							    (c)->sp-((uintptr_t)(stack)+(size)-SP_DELTA);

#define context_save(c)  arch_context_save(c)


#endif /* __EZA_CONTEXT_H__ */

