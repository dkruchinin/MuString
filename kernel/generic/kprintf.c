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
 * (c) Copyright 2006,2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mstring/generic_api/kprintf.c
 *
 */

#include <mstring/stdarg.h>
#include <mstring/string.h>
#include <mstring/ctype.h>
#include <mstring/kprintf.h>
#include <mstring/kconsole.h>
#include <arch/types.h>

#define TMPBUF_SIZE  1024
#define char2int(c) ((int)(c - '0'))

typedef long long dlong;
typedef unsigned long long udlong;

/* structure for format description */
struct fattrs {  
  int width;      /* format width (before dot) */
  int presiction; /* format presiction (after dot)*/
  int cinf;       /* number of characters in format */
  uint32_t flags; /* format flags */  
};

static size_t __digit(char *, const size_t, dlong, struct fattrs *);
static bool __copy_byte_ip(char *, const size_t, const char);
static void __ident_num_size(dlong *, va_list, const struct fattrs *);
static void __fields_inc(int *, const int);
static size_t __make_width(char*, const size_t, const char, const struct fattrs *);

static struct kbuffer __kbuf = {
  .cur_size = 0,
  .size = KBUF_SIZE
};

size_t kbuf_insert(const char *s, const size_t slen)
{
  return slen; /* FIXME: this is only temporary solution */
  if(slen > __kbuf.size)
	return -1;
  if(slen > (__kbuf.size - __kbuf.cur_size)) {
	memset(&__kbuf, '\0', __kbuf.cur_size);
	__kbuf.cur_size = 0;
  }

  strncpy(__kbuf.space + __kbuf.cur_size, s, slen);
  __kbuf.cur_size += slen;

  return slen;
}


char* kbuf_get(void)
{
  if(__kbuf.cur_size == 0)
	return NULL;

  return __kbuf.space + __kbuf.cur_size;
}

void kprintf(const char *fmt, ...)
{
  va_list ap;
  
  va_start(ap, fmt);
  vkprintf(default_console(), fmt, ap);
  va_end(ap);
}

void kprintf_fault(const char *fmt, ...)
{
  va_list ap;
  kconsole_t *fault_cons = fault_console();
  
  va_start(ap, fmt);
  if (unlikely(!fault_cons->is_enabled))
    fault_cons->enable();
  
  vkprintf(fault_cons, fmt, ap);
  va_end(ap);
}

size_t sprintf(char *str, const char *fmt, ...)
{
  va_list ap;
  size_t len;

  va_start(ap, fmt);
  len = vsprintf(str, fmt, ap);
  va_end(ap);
  return len;
}

size_t snprintf(char *str, size_t size, const char *fmt, ...)
{
  va_list ap;
  size_t len;  

  va_start(ap, fmt);
  len = vsnprintf(str, size, fmt, ap);
  va_end(ap);
  return len;
}

void vkprintf(kconsole_t *kcons, const char *fmt, va_list ap)
{
  char tmp_buf[TMPBUF_SIZE];
  size_t sl;

  memset(tmp_buf, '\0', TMPBUF_SIZE);  
  sl = vsprintf(tmp_buf, fmt, ap);
  /* FIXME: fix this shit... */
  /*if (sl > TMPBUF_SIZE) {
	kcons->display_string("\nvkprintf error\n");
	return;
    }
  }*/
  if (kbuf_insert(tmp_buf, sl) != sl) {
    kcons->display_string("\n__kbuf error\n");
    return;
  }
  if (kcons->is_enabled)
    kcons->display_string(tmp_buf);
}

size_t vsprintf(char *dest, const char *fmt, va_list ap)
{
  return vsnprintf(dest, ~(0U) >> 1, fmt, ap);
}

size_t vsnprintf(char *dest, const size_t dsize, const char *fmt, va_list ap)
{
  bool pers = false; /* if '%' found */
  size_t cs = 0, ret, len;
  dlong d;
  char c, *s;
  struct fattrs fa;

  memset(&fa, 0, sizeof(fa));
  c = 0; s = NULL;
  for(; *fmt != '\0'; fmt++) {
	if((*fmt == '%') && !pers) {
	  pers = true;
	  continue;
	}

	if(pers) {
	  pers = false;
	  fa.cinf++;

	  /* grub fields digits */
	  if(isdigit(*fmt)) {
		if(fa.flags & FMT_LMASK)
		  break;

		pers = true;
		
		if(fa.flags & FMT_DOT) {
		  __fields_inc(&fa.presiction, char2int(*fmt));
		  continue;
		}
		else {
		  if(*fmt >= '0') {
			__fields_inc(&fa.width, char2int(*fmt));
			continue;
		  }
		}
	  }

	  switch(*fmt) {
	  case 'i': case 'd':
		fa.flags |= FMT_TNUM | FMT_DDEC;
		__ident_num_size(&d, ap, &fa);
		goto BC;
	  case 'c':
		c = va_arg(ap, int);
		fa.flags |= FMT_TCHAR;
		goto BC;
	  case 's':
		if((s = va_arg(ap, char *)) == NULL)
		  s = "<NULL>";

		fa.flags |= FMT_TSTR;
		goto BC;
	  case 'x':
		fa.flags |= FMT_TNUM | FMT_DHEX | FMT_DUNSIG;
		__ident_num_size(&d, ap, &fa);
		goto BC;
	  case 'X':
		fa.flags |= FMT_TNUM | FMT_DHEX_UP | FMT_DUNSIG;
		__ident_num_size(&d, ap, &fa);
		goto BC;
	  case 'p':
        fa.flags |= FMT_TNUM | FMT_DHEX | FMT_FPREF | FMT_LLONG | FMT_DUNSIG;
        __ident_num_size(&d, ap, &fa);
		goto BC;
	  case 'o':
		fa.flags |= FMT_TNUM | FMT_DOCT | FMT_DUNSIG;
		__ident_num_size(&d, ap, &fa);
		goto BC;
	  case 'u':
		fa.flags |= FMT_TNUM | FMT_DDEC | FMT_DUNSIG;
		__ident_num_size(&d, ap, &fa);
		goto BC;
	  case '#':
		if((fa.flags & FMT_LMASK) || (fa.flags & FMT_DOT) ||
		   (fa.width > 0)) {
		  break;
		}
		
		fa.flags |= FMT_FPREF;
		pers = true;
		
		continue;
	  case '+':
		if((fa.flags & FMT_LMASK) || (fa.flags & FMT_DOT) ||
		   (fa.width > 0))
		  break;
		if(fa.flags & FMT_FSPACE)
		  fa.flags &= ~FMT_FSPACE;
		
		fa.flags |= FMT_FSIG;
		pers = true;
		continue;
	  case '-':
		if((fa.flags & FMT_LMASK) || (fa.flags & FMT_FSPACE) ||
		   (fa.flags & FMT_DOT) || (fa.width > 0))
		  break;
		if(fa.flags & FMT_FZERO)
		  fa.flags  &= ~FMT_FZERO;
		
		
		pers = true;
		fa.flags |= FMT_FLJ;

		continue;
	  case '0':
		if((fa.flags & FMT_LMASK) || (fa.flags & FMT_DOT) ||
		   (fa.width > 0))
		  break;
		if(!(fa.flags & FMT_FLJ))
		  fa.flags |= FMT_FZERO;

		pers = true;
		continue;
	  case ' ':
		if((fa.flags & FMT_FLJ) || (fa.flags & FMT_LMASK) ||
		   (fa.flags & FMT_DOT || (fa.width > 0)))
		  break;
		if(!(fa.flags & FMT_FSIG))
		  fa.flags |= FMT_FSPACE;
		
		pers = true;
		continue;
	  case '.':
		if(fa.flags & FMT_FZERO)
		  fa.flags &= ~FMT_FZERO;
		
		pers = true;
		fa.flags |= FMT_DOT;
		
		continue;
	  case 'h':
		if(*(fmt + 1) == 'h') {
		  fmt++;
		  fa.flags |= FMT_LCHAR;
		}
		else
		  fa.flags |= FMT_LSHORT;
		
		pers = true;
		continue;
	  case 'l':
		if(*(fmt + 1) == 'l') {
		  fmt++;
		  fa.flags |= FMT_LDLONG;
		}
		else
		  fa.flags |= FMT_LLONG;
		
		pers = true;
		continue;
	  case 'z':
		fa.flags |= FMT_LSIZET;
		pers = true;
		continue;
	  case '*':
		if((fa.flags & FMT_FMASK) || (fa.flags & FMT_LMASK))
		  break;

		if(fa.flags & FMT_DOT) {
		  if(fa.presiction > 0)
			break;

		  __ident_num_size((dlong*)(&fa.presiction), ap, &fa);
		}
		else {
		  if(fa.width > 0)
			break;

		  __ident_num_size((dlong*)(&fa.width), ap, &fa);
		}

		pers = true;
		continue;
	  case '%':
		fa.cinf = 0;
		break;
	  }
	}

   	fmt -= fa.cinf;

	if(!__copy_byte_ip(dest + cs, dsize - cs, *fmt))
	  return dsize;

	cs++;
	memset(&fa, 0, sizeof(fa));
	continue;	
  BC:
	
	if((fa.flags & FMT_TNUM) == FMT_TNUM) {
	  if((ret = __digit(dest + cs, dsize - cs, d, &fa)) == 0)
		return dsize;

	  cs += ret;
	}
	else if((fa.flags & FMT_TSTR) == FMT_TSTR) {
	  len = strlen(s);	  
	  fa.width = ((fa.width <= len) ? 0 : fa.width - len);  

	  if(!(fa.flags & FMT_FLJ)) {
		ret = __make_width(dest + cs, dsize - cs, ' ', &fa);
		cs += ret;
	  }
	  if(len <= (dsize - cs)) {
		memcpy(dest + cs, s, len);
		cs += len;
	  }
	  if(fa.flags & FMT_FLJ) {
		ret = __make_width(dest + cs, dsize - cs, ' ', &fa);
		cs += ret;
	  }
	}
	else {
	  fa.width = ((fa.width <= 1) ? 0 : fa.width - 1);
	  if(!(fa.flags & FMT_FLJ)) {
		ret = __make_width(dest + cs, dsize - cs, ' ', &fa);
		cs += ret;
	  }
	  if(!__copy_byte_ip(dest + cs, dsize - cs, c))
		return dsize;

	  cs++;
	  
	  if(fa.flags & FMT_FLJ) {
		ret = __make_width(dest + cs, dsize - cs, ' ', &fa);
		cs += ret;
	  }
	}

	memset(&fa, 0, sizeof(fa));
  }

  return cs;
}

/*
 * Convert number to string and write it to dest.
 */
static size_t __digit(char *dest, const size_t dsize, dlong number, struct fattrs *fa)
{
  char tmp_buf[FMT_DBUF_SIZE];
  char nums[] = "0123456789abcdef", c;
  unsigned long base, i;
  udlong d;
  bool neg = false;
  size_t j, cs = 0, ret;
  
  memset(tmp_buf, '\0', FMT_DBUF_SIZE);
  
  /*
   * To prevent overflow, take care about number's sign
   * and store ABS(number) into d
   */
  if((number < 0) && !(fa->flags & FMT_DUNSIG)) {
	neg = true;
	d = -number;
	fa->flags &= ~FMT_FSPACE;
  }
  else
    d = (udlong)number;

  /* determine base */
  if(fa->flags & FMT_DOCT)
	base = 8;
  else if(fa->flags & FMT_DDEC)
	base = 10;
  else if((fa->flags & FMT_DHEX) || (fa->flags & FMT_DHEX_UP)) {
	base = 0x10;

	if(fa->flags & FMT_DHEX_UP) {
	  for(i = 0x0A; i < base; i++)
		nums[i] -= 32;
	}
  }
  else /* something going wrong... */
	base = 1;

  j = 0;
  /* copy digit sequence to the temporary buffer */
  do {
    i = d % base;
    d = d / base;
	tmp_buf[j++] = nums[i];
  } while(d != 0);

  fa->width = ((fa->width <= j) ? 0 : fa->width - j);
  fa->presiction = ((fa->presiction <= j) ? 0 : fa->presiction - j);
  fa->width = ((fa->width <= fa->presiction) ? 0 : fa->width - fa->presiction);  

  if(fa->flags & FMT_FSPACE) {
	if(!__copy_byte_ip(dest + cs, dsize - cs, ' '))
	  return dsize;

	cs++;
  }
  if(!(fa->flags & FMT_FLJ)) {
	c = ((fa->flags & FMT_FZERO) ? '0' : ' ');
	ret = __make_width(dest + cs, dsize - cs, c, fa);
	cs += ret;
  }
  if((fa->flags & FMT_DHEX) || (fa->flags & FMT_DHEX_UP)) {
	c = ((fa->flags & FMT_DHEX_UP) ? 'X' : 'x');

	if(fa->flags & FMT_FPREF) {
	  if(!__copy_byte_ip(dest + cs, dsize - cs, '0'))
		return dsize;
	  
	  cs++;
	  
	  if(!__copy_byte_ip(dest + cs, dsize - cs, c))
		return dsize;
	  
	  cs++;
	}
  }
  else if(fa->flags & FMT_DDEC) {
	c = '\0';

	if((fa->flags & FMT_FSIG) && !neg)
	  c = '+';
	if(neg)
	  c = '-';
	if(c != '\0') {
	  if(!__copy_byte_ip(dest + cs, dsize - cs, c))
		return dsize;

	  cs++;
	}
  }
  else if(fa->flags & FMT_DOCT) {
	if(fa->flags & FMT_FPREF) {
	  if(!__copy_byte_ip(dest + cs, dsize - cs, '0'))
		return dsize;

	  cs++;
	}
  }

  if((fa->presiction > 0) && (fa->presiction <= (dsize - cs))) {
	memset(dest + cs, '0', fa->presiction);
	cs += fa->presiction;
  }

  if(j > (i = dsize - cs))
	j = i;  

  while(j-- != 0)
	dest[cs++] = tmp_buf[j];
  
  if(fa->flags & FMT_FLJ) {
	ret = __make_width(dest + cs, dsize - cs, ' ', fa);
	cs += ret;
  }

  return cs;
}

/* copy byte if possible */
static bool __copy_byte_ip(char *dest, const size_t dsize, const char c)
{
  if(dsize == 0) {
	return 0;
  }

  *dest = c;
  return 1;
}

static void __ident_num_size(dlong *d, va_list ap, const struct fattrs *fa)
{
  if(fa->flags & FMT_LLONG)
    *d = va_arg(ap, long);
  else if(fa->flags & FMT_LDLONG)
	*d = va_arg(ap, dlong);
  else if(fa->flags & FMT_LSIZET)
	*d = va_arg(ap, size_t);
  else {
	*d = va_arg(ap, int);
	if(fa->flags & FMT_DUNSIG)
	  *d = (unsigned int)*d;
  }
}

static void __fields_inc(int *num, const int incr)
{
  *num *= 10;
  *num += incr;
}

static size_t __make_width(char *dest, const size_t dsize, const char c, const struct fattrs *fa)
{
  if((fa->width == 0) || (fa->width > dsize))
	return 0;

  memset(dest, c, fa->width);
  return fa->width;
}
