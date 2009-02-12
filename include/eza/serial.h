#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <eza/arch/asm.h>

#define SERIAL_PORT 0x3f8   /* COM1 */

static inline int serial_is_transmit_empty() {
   return inb(SERIAL_PORT + 5) & 0x20;
}

static inline void serial_write_char(char a) {
  while (serial_is_transmit_empty() == 0);

  if( a == '\n' ) {
    outb(SERIAL_PORT,'\r');
    while (serial_is_transmit_empty() == 0);
  }
  outb(SERIAL_PORT,a);
}


#endif
