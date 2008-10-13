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
 * (c) Copyright 2005 Dan Kruchinin
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/generic_api/kconsole.c: default outputting system
 *                          
 *
 */

#include <eza/arch/types.h>
#include <eza/vga.h>
#include <eza/spinlock.h>
#include <eza/kconsole.h>

/* VGA console */
static void vga_cons_enable(void);
static void vga_cons_disable(void);
static void vga_cons_display_string(const char *);
static void vga_cons_display_char(const char);

static kconsole_t __vga_cons = {
  .enable = vga_cons_enable,
  .disable = vga_cons_disable,
  .display_string = vga_cons_display_string,
  .display_char = vga_cons_display_char,
};

static void vga_cons_enable(void)
{
  vga_init();
  vga_cursor(true);
  vga_set_cursor_attrs(VGA_CRSR_LOW);
  vga_set_bg(KCONS_DEF_BG);
  vga_set_fg(KCONS_DEF_FG);
  vga_cls();
  __vga_cons.is_enabled = true;
  spinlock_initialize(&__vga_cons.lock);
}

static void vga_cons_disable(void)
{
  __vga_cons.is_enabled = false;
}

static void vga_cons_display_string(const char *str)
{
  int i;

  if (!__vga_cons.is_enabled)
    return;
  
  spinlock_lock(&__vga_cons.lock);
  for(i = 0; str[i] != '\0'; i++)
    vga_putch(str[i]);

  vga_update_cursor();
  spinlock_unlock(&__vga_cons.lock);
}
 
static void vga_cons_display_char(const char c)
{
  if (__vga_cons.is_enabled)
    return;

  spinlock_lock(&__vga_cons.lock);
  vga_putch(c);
  vga_update_cursor();
  spinlock_unlock(&__vga_cons.lock);
}

kconsole_t *default_console()
{
  return &__vga_cons;
}
