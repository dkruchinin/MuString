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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mlibc/stdlib.c: kernel implementation general-purpose routines.
 *
 */

#include <mlibc/stdlib.h>
#include <eza/arch/types.h>

void itoa(int num,char *buf,size_t buflen,unsigned int radix)
{
  char table10[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
  char table16[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                    'A', 'B', 'C', 'D', 'E', 'F' };
  char *table;
  char *sbuf = buf;

  switch( radix ) {
    case 10:
    case 8:
      table = table10;
      break;
    case 16:
      table = table16;
      break;
    default:
      table = NULL;
      break;
  }

  if( table != NULL && buflen > 0 ) {
    if( num < 0 && radix == 10 ) {
      *buf++ = '-';
      buflen--;
    }
    while( buflen > 0 ) {
      if( num < radix ) {
        *buf++ = table[num];
        break;
      } else {
        *buf++ = table[num % radix];
        num /= radix;
      }
      buflen--;
    }
    if( buflen > 0 ) {
      *buf = '\0';
    }

    /* Now reverse number. */
    buf--;
    while( sbuf < buf ) {
      char c = *sbuf;
      *sbuf = *buf;
      *buf = c;
      sbuf++;
      buf--;
    }
  }
}

