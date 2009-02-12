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
 * include/eza/kconsole.h: base console
 *                     
 *
 */


#ifndef __EZA_KCONSOLE_H__
#define __EZA_KCONSOLE_H__

#include <eza/arch/types.h>
#include <eza/vga.h>
#include <eza/spinlock.h>

#ifndef KCONS_DEF_BG
#define KCONS_DEF_BG VC_BLACK
#endif /* KCONS_DEF_BG */
#ifndef KCONS_DEF_FG
#define KCONS_DEF_FG VC_WHITE
#endif /* KCONS_DEF_FG */
#define KCONS_COLOR_MODE  1
#ifdef KCONS_COLOR_MODE
#ifndef KCONS_WARN_BG
#define KCONS_WARN_BG VC_BLACK
#endif /* KCONS_WARN_BG */
#ifndef KCONS_WARN_FG
#define KCONS_WARN_FG VC_RED
#endif /* KCONS_WARN_FG */
#ifndef KCONS_WARN_BL
#define KCONS_WARN_BL 1
#endif /* KCONS_WARN_BL */
#ifndef KCONS_ERR_BG
#define KCONS_ERR_BG VC_BLACK
#endif /* KCONS_ERR_BG */
#ifndef KCONS_ERR_FG
#define KCONS_ERR_FG VC_RED
#endif /* KCONS_ERR_BG */
#ifndef KCONS_ERR_BL
#define KCONS_ERR_BL 1
#endif /* KCONS_ERR_BL */
#endif /* KCONS_COLOR_MODE */     

typedef struct __kconsole_type {
  void (*enable)(void);
  void (*display_string)(const char*);
  void (*display_char)(const char);
  void (*disable)(void);
  spinlock_t lock;
  bool is_enabled;
} kconsole_t;

kconsole_t *default_console(void);
kconsole_t *get_fault_console(void);
void set_default_console(kconsole_t *cons);

#define PREPARE_FAULT_CONSOLE()  do {           \
  set_default_console(get_fault_console());     \
  if (!default_console()->is_enabled)           \
    default_console()->enable();                \
  } while(0)

#endif /* __EZA_KCONSOLE_H__ */
