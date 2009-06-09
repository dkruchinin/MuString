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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/mstring/vga.h: VGA support on initing
 *                     
 *
 */

#ifndef __EZA_VGA_H__
#define __EZA_VGA_H__

#include <config.h>
#include <mstring/types.h>

/* VGA memory base address */
#define VGA_TM_BASE 0xB8000 /* text mode */
#define VGA_VM_BASE 0xA0000 /* video mode */

/* modes */
#define VGA_MONO  0x0B0 /* mono mode */
#define VGA_COLOR 0x0D0 /* color mode */

/* VGA data register ports */
#define VDP_CRT     0x305 /* CRTC Controller Data Register (0x0B0 - mono; 0x0D0 - color) */
#define VDP_ATR_WR  0x3C0 /* Attribute Controller Data Write Register */
#define VDP_ATR_RD  0x3C1 /* Attribute Controller Data Read Register */
#define VDP_SDR     0x3C5 /* Sequencer Data Register */
#define VDP_DDR     0x3C9 /* DAC Data Register */
#define VDP_FCR_RD  0x3CA /* Feature Control Register */
#define VDP_MOR_WR  0x3C2 /* Miscellaneous Output Write Register */
#define VDP_MOR_RD  0x3CC /* Miscellaneous Output Read Register */
#define VDP_GCDR    0x3CF /* Graphics Controller Data Register */
#define VDP_ISR1    0x30A /* Input Status #1 Register (0x0B0 - mono; 0x0D0 - color) */

/* VGA address register ports */
#define VAP_CRT     0x304 /* CRTC Controller Address Register (0x0B0 - mono; 0x0D0 -color) */
#define VAP_ATR_WR  0x3C0 /* Attribute Address Write Register */
#define VAP_SAR     0x3C5 /* Sequencer Address Register */
#define VAP_DAR_RD  0x3C7 /* DAC State Register */
#define VAP_DAR_WR  0x3C8 /* DAC Address Write Mode Register */
#define VAP_GCAR    0x3CE /* Graphics Controller Address Register */

/* CRTC indexes defenition */
#define NCRTCI  25   /* number of CRTC indexes */
#define HTR     0x00 /* Horizontal Total Register */
#define EHDR    0x01 /* End Horizontal Display Register */
#define SHBR    0x02 /* Start Horizontal Blanking Register */
#define EHBR    0x03 /* End Horizontal Blanking Register */
#define SHRR    0x04 /* Start Horizontal Retrace Register */
#define EHRR    0x05 /* End Horizontal Retrace Register */
#define VTR     0x06 /* Vertical Total Register */
#define OVR     0x07 /* Overflow Register */
#define PRSR    0x08 /* Preset Row Scan Register */
#define MSLR    0x09 /* Maximum Scan Line Register */
#define CSR     0x0A /* Cursor Start Register */
#define CER     0x0B /* Cursor End Register */
#define SAHR    0x0C /* Start Address High Register */
#define SALR    0x0D /* Start Address Low Register */
#define CLHR    0x0E /* Cursor Location High Register */
#define CLLR    0x0F /* Cursor Location Low Register */
#define VRSR    0x10 /* Vertical Retrace Start Register */
#define VRER    0x11 /* Vertical Retrace End Register */
#define VDER    0x12 /* Vertical Display End Register */
#define OFR     0x13 /* Offset Register */
#define ULR     0x14 /* Underline Location Register */
#define SVBR    0x15 /* Start Vertical Blanking Register */
#define EVBR    0x16 /* End Vertical Blanking */
#define CMCR    0x17 /* CRTC Mode Control Register */
#define LCR     0x18 /* Line Compare Register */

/* Attribute indexes defenition */
#define NATRI  5    /* number of attribute indexes */
#define AMCR   0x10 /* Attribute Mode Control Register */
#define OCR    0x11 /* Overscan Color Register */
#define CPER   0x12 /* Color Plane Enable Register */
#define HPPR   0x13 /* Horizontal Pixel Panning Register */
#define CLSR   0x14 /* Color Select Register */

/* some major key codes */
#define BACKSPACE  0x08
#define TAB        0x09
#define SPACE      0x20

/* CSRT register flags */
#define VGA_CDISABLE  0x20
#define VGA_CENABLE   ((uint8_t)~0x20)

/* Attribute register flags */
#define VGA_ABLINK_ENABLE   0x08             /* blinking enable */
#define VGA_ABLINK_DISABLE  ((uint8_t)~0x08) /* blinking disable */

/* misc flags for VGA functions */
#define VGA_CRSR_LOW   0x01  /* line cursor */
#define VGA_CRSR_HIGH  0x02  /* block cursor */
#define VGA_CRSR_BLIK  0x04  /* blinking cursor */

/* get reading or writing register address(mono or color depend on type) */
#define vga_set_mode(addr, type)  ((addr) + (type))

/* structure used for VGA manipulation */

typedef struct __vga_font_type {
  uint16_t wide;
  uint16_t high;
} vgafont_t;

/* 16 major colors */
typedef enum __vga_colors {
  VC_BLACK = 0,
  VC_BLUE,
  VC_GREEN,
  VC_CYAN,
  VC_RED,
  VC_MAGENTA,
  VC_BROWN,
  VC_WHITE,
  VC_LIGHT_BLUE,
  VC_LIGHT_GREEN,
  VC_LIGHT_RED,
  VC_LIGHT_BROWN,
  VC_LIGHT_GREY,
} vga_color_t;

typedef struct __vga_dev_type {
  uint16_t* base;
  uint16_t cols;
  uint16_t rows;
  uint16_t x;
  uint16_t y;
  uint16_t es_char;
  vga_color_t fg;
  vga_color_t bg;  
  vgafont_t font;  
} vgadev_t;

typedef struct __vga_regs_type {
  uint8_t crtc[NCRTCI];
  uint8_t atr[NATRI];
} vgaregs_t;

void vga_init(void);

/* Coordinates-functions */
uint16_t vga_get_x(void);
uint16_t vga_get_y(void);
void vga_set_x(const uint16_t x);
void vga_set_y(const uint16_t y);
uint16_t vga_get_cols(void);
uint16_t vga_get_rows(void);

/* Fcuntion working with VGA colors */
void vga_set_fg(vga_color_t fg);
void vga_set_bg(vga_color_t bg);
vga_color_t vga_get_fg(void);
vga_color_t vga_get_bg(void);

void vga_update_cursor(void);
void vga_cursor(const bool action);
void vga_set_cursor_attrs(const uint8_t flags);
void vga_blinking(const bool action);
void vga_putch(uint16_t c);
void vga_cls(void);
void vga_scroll(void);

#endif /* __EZA_VGA_H__ */
