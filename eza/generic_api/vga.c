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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/generic_api/vga.c: simple VGA functions
 *                          
 *
 */

#include <eza/arch/types.h>
#include <eza/vga.h>
#include <mlibc/string.h>
#include <eza/arch/asm.h>

static vgadev_t __vga;
static vgaregs_t __vregs;

void vga_set_x(const uint16_t x)
{
  __vga.x = x;
}

void vga_set_y(const uint16_t y)
{
  __vga.y = y;
}

void vga_set_fg(vga_color_t fg)
{
  __vga.fg = fg;
}

void vga_set_bg(vga_color_t bg)
{
  __vga.bg = bg;
}

uint16_t vga_get_cols(void)
{
  return __vga.cols;
}

uint16_t vga_get_rows(void)
{
  return __vga.rows;
}

vga_color_t vga_get_fg(void)
{
  return __vga.fg;
}

vga_color_t vga_get_bg(void)
{
  return __vga.bg;
}

uint16_t vga_get_x(void)
{
  return __vga.x;
}

uint16_t vga_get_y(void)
{
  return __vga.y;
}

inline void vga_make_attr(uint16_t* out)
{
  *out |= ((uint16_t)(__vga.fg) << 8) | ((uint16_t)(__vga.bg) << 12);
}

void vga_init(void)
{
  __vga.base = (uint16_t *)VGA_TM_BASE;
  __vga.cols = 80;
  __vga.rows = 25;
  __vga.x = __vga.y = 0;
  memset(&__vregs, 0, sizeof(__vregs));
  __vga.es_char = -1;
  vga_cls();
}

void vga_update_cursor(void)
{
  uint16_t where, crt_dp, crt_ap;
  uint16_t crt_ar;

  where=__vga.y * __vga.cols + __vga.x ;
  crt_dp=vga_set_mode(VDP_CRT, VGA_COLOR); /* CRT data port */
  crt_ap=vga_set_mode(VAP_CRT, VGA_COLOR); /* CRT address port */

  /* i/o all */
  crt_ar = inb(crt_ap);
  outb(crt_ap, CLHR);
  __vregs.crtc[CLHR] = where >> 8;
  outb(crt_dp,__vregs.crtc[CLHR]);
  outb(crt_ap,CLLR);
  __vregs.crtc[CLLR]=where & 0xFFFFFFFF;
  outb(crt_dp,__vregs.crtc[CLLR]);
  outb(crt_ap,crt_ar);
}

void vga_cursor(const bool action)
{
  uint16_t crt_dp,crt_ap,crt_ar;

  crt_dp=vga_set_mode(VDP_CRT,VGA_COLOR);
  crt_ap=vga_set_mode(VAP_CRT,VGA_COLOR);

  crt_ar=inb(crt_ap);
  outb(crt_ap,CSR);
  __vregs.crtc[CSR]=inb(crt_dp);

  if(!action && !(__vregs.crtc[CSR] & VGA_CDISABLE))
    outb(crt_dp, __vregs.crtc[CSR] | VGA_CDISABLE);
  if(action && (__vregs.crtc[CSR] & VGA_CDISABLE))
    outb(crt_dp, __vregs.crtc[CSR] & VGA_CENABLE);
  outb(crt_ap, crt_ar);

  return;
}

void vga_set_cursor_attrs(const uint8_t flags)
{
  uint16_t crt_dp, crt_ap, crt_ar;
  
  crt_dp=vga_set_mode(VDP_CRT, VGA_COLOR);
  crt_ap=vga_set_mode(VAP_CRT, VGA_COLOR);
  crt_ar=inb(crt_ap);
  
  if(flags & VGA_CRSR_LOW) {
    vga_putch('C');
    outb(crt_ap, CSR);
    __vregs.crtc[CSR]=inb(crt_dp);
    outb(crt_dp, 0x0A);
  }
  if(flags & VGA_CRSR_HIGH){
    vga_putch('G');
    outb(crt_ap, CER);
    __vregs.crtc[CER]=inb(crt_dp);
    outb(crt_dp, 0x0B);
  }
  outb(crt_ap, crt_ar);

  return;
}

void vga_blinking(const bool action)
{
  uint16_t isr1_dp, isr1_save, atr_save;
  
  isr1_dp=vga_set_mode(VDP_ISR1,VGA_COLOR);
  isr1_save=inb(isr1_dp);
  atr_save=inb(VDP_ATR_RD);
  outb(VAP_ATR_WR,AMCR);
  __vregs.atr[AMCR]=inb(VDP_ATR_RD);

  if(action && !(__vregs.atr[AMCR] & VGA_ABLINK_ENABLE))
    outb(VAP_ATR_WR,__vregs.atr[AMCR] | VGA_ABLINK_ENABLE);
  if(!action && (__vregs.atr[AMCR] & VGA_ABLINK_ENABLE))
    outb(VAP_ATR_WR,__vregs.atr[AMCR] & VGA_ABLINK_DISABLE);

  outb(VAP_ATR_WR, atr_save);
  outb(isr1_dp, isr1_save);

  return;
}

void vga_putch(uint16_t c)
{
  uint16_t *where;

  switch(c) {
  case BACKSPACE:
    if(__vga.x!=0)
      __vga.x--;
    break;
  case TAB:
    __vga.x+=8 & ~(8 - 1);
    break;
  case '\r':
    __vga.x=0;
    break;
  case '\n':
    __vga.x=0; /* increment to 0 later */
    __vga.y++;
    break;
  default:
    if(c < ' ')
      break;
    vga_make_attr(&c);
    where=(uint16_t *)(__vga.base+__vga.cols*__vga.y+__vga.x);
    *where = c;
    __vga.x++;
    break;
  }

  vga_scroll();

  return;
}

void vga_cls(void)
{
  uint16_t out;
  int i, lim = __vga.cols * __vga.rows;

  out=SPACE;
  vga_make_attr(&out);
  for (i = 0; i < lim; i++)
    __vga.base[i] = out;
  
  __vga.x=__vga.y=0;
  vga_update_cursor();
}

void vga_scroll(void)
{
  uint16_t out,*vgam;
  int i=0;

  if(__vga.x>=__vga.cols) {
    __vga.y++;
    __vga.x=0;
  }

  if(__vga.y>=__vga.rows) {
    out=SPACE;
    vga_make_attr(&out);
    memcpy((void *)__vga.base, (void *) (__vga.base + (__vga.cols)),2*((__vga.cols * __vga.rows)-__vga.cols));
    vgam=(uint16_t *)(__vga.base+__vga.cols*(__vga.rows-1));
    for(i=0;i<__vga.cols;i++)
      vgam[i]=out;
    __vga.y=__vga.rows-1;
  }

  return;
}

