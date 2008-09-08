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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * mm/bbuddy.c: binary buddy abstraction implementation
 *              part of MuiString mm
 *
 */

#include <eza/arch/types.h>
#include <mm/bbuddy.h>

/* indexing */
#define bbuddy_indexp(p,n)  (((p >> 5) > 1) ? ((p >> 5)+(n >> 5)) : p >> 5)
#define bbuddy_indexbn(n)   ((n >> 5) ? n%32 : n)

/* local functions prototypes */
static uint32_t __bbuddy_block_alloc(bbuddy_t *b,uint32_t align,uint8_t m_flag);
static uint32_t __bbuddy_split_up(bbuddy_t *b,uint32_t align,uint32_t num);
static uint32_t __bbuddy_block_release(bbuddy_t *b,uint32_t num,uint32_t i);

/* init buddy system with already allocated pointer */
uint8_t bbuddy_init(bbuddy_t *b,uint32_t max_part)
{
  int yy=((max_part >> 5) << 1),i;
  char *ptr=(char*)b;

  if(b && max_part) {
    ptr+=sizeof(bbuddy_t);
    b->pbmp=(uint32_t*)ptr;
    ptr+=(sizeof(uint32_t)*(((max_part >> 5) > 1) ? (max_part >> 5)+2 : (max_part >> 5)+1));
    b->nbmp=(uint32_t*)ptr;
  } else
    return 1;

  b->pn=max_part;

  max_part>>=5;

  for(i=0;i<yy;i++) {
    b->nbmp[i]=nil;
    b->pbmp[i]=fil;
  }

  /* init first */
  b->nbmp[0] |= (1 << 0);
  b->nbmp[0] |= (1 << 1);

  return 0;
}

/* allocate block with the given align
 * level, returns index in align level
 */
uint32_t bbuddy_block_alloc(bbuddy_t *b, uint32_t align)
{
  return __bbuddy_block_alloc(b,align,nil);
}

/* release block, num is points to the smaller 
 * blocks counter
 */
uint32_t bbuddy_block_release(bbuddy_t *b,uint32_t num)
{
  return __bbuddy_block_release(b,num,0);
}

static uint32_t __bbuddy_block_release(bbuddy_t *b,uint32_t num,uint32_t i)
{
  uint32_t p_indx,n_indx;
  uint32_t layer=b->pn,ls=layer,a=0;

  /* first look up on higher possible layer
   * if there are now, look deeper
   */
  if(num>b->pn)
    return EINVALIDINDX; /* error encount */

  while(ls) {    ls>>=1; a++;  } 
  if(i==0) {
    for(i=(a-1);i>0;i--) {
      if(num%2) break;
      else num>>=1;
    }
  } 

  layer=(1 << i);

  n_indx=(layer<32) ? (num+layer) : bbuddy_indexbn(num);
  p_indx=bbuddy_indexp(layer,num);
  if(!(b->nbmp[p_indx] & (1 << n_indx)) && (b->pbmp[p_indx] & (1 << n_indx))) { 
    b->nbmp[p_indx] |= (1 << n_indx); /* mark free */
    return __bbuddy_split_up(b,layer,num);
  } else if(!(b->nbmp[p_indx] & (1 << n_indx)) && !(b->pbmp[p_indx] & (1 << n_indx))) { /* going deep */
    i++;
    if(i>=a)
      return EBUDDYCORRUPED;
    else
      return __bbuddy_block_release(b,num*2,i);

  }

  return 0;
}

static uint32_t __bbuddy_split_up(bbuddy_t *b,uint32_t align,uint32_t num)
{
  uint32_t p_indx=bbuddy_indexp(align,num), n_indx=(align<32) ? (num+align) : bbuddy_indexbn(num);
  int16_t succ=((num%2) ? -1 : 1);

  if(b->nbmp[p_indx] & (1 << (n_indx+succ))) { /* can be splitted */
    b->nbmp[p_indx] &= ~(1 << (n_indx+succ)); /* mark */
    b->nbmp[p_indx] &= ~(1 << (n_indx));
    b->pbmp[p_indx] |= ~(1 << (n_indx+succ));
    b->pbmp[p_indx] |= ~(1 << (n_indx));
    /* mark parent non-separated and free */
    if(succ<0)      num--;
    align>>=1; num>>=1;
    n_indx=(align<32) ? (num+align) : bbuddy_indexbn(num);
    p_indx=bbuddy_indexp(align,num);
    b->nbmp[p_indx] |= (1 << n_indx);
    b->pbmp[p_indx] |= (1 << n_indx);
    if(align>1)
      return __bbuddy_split_up(b,align,num);
  }

  return 0;
}

/* allocate block, returns number of block in bitmap area */
static uint32_t __bbuddy_block_alloc(bbuddy_t *b,uint32_t align,uint8_t m_flag)
{
  uint32_t p_indx=align>>5;
  uint32_t n_indx=0,i=0,o=0;

  if(p_indx) {
    for(i=0;i<p_indx;i++) {
      if(b->nbmp[p_indx+i]!=0x0)
	goto __is_free_long;
    }
    m_flag++;
    return __bbuddy_block_alloc(b,align/2,m_flag);
  }

  if(!p_indx) { /* checking small layers, big blocks */
    o=align << 1;
    for(i=align;i<o;i++) 
      if((b->nbmp[p_indx] & (1 << i)) && !m_flag) {
	b->nbmp[p_indx] &= ~(1 << i); /* used */
	b->pbmp[p_indx] |= (1 << i); /* not separated */
	return i-align;
      } else if((b->nbmp[p_indx] & (1 << i)) && m_flag) {
	b->nbmp[p_indx] &= ~(1 << i); /* used */
	b->pbmp[p_indx] &= ~(1 << i); /* separated */
	/* mark childs free */
	align<<=1;	 i<<=1;
	p_indx=align >> 5;
	n_indx=bbuddy_indexbn(i);
	b->nbmp[p_indx] |= (1 << n_indx);
	b->nbmp[p_indx] |= (1 << (n_indx+1));
	b->pbmp[p_indx] |= (1 << n_indx);
	b->pbmp[p_indx] |= (1 << (n_indx+1));

	m_flag--;
	return __bbuddy_block_alloc(b,align,m_flag);
      }
    if(i<2)
      return ENOBLOCK;
    else {
      m_flag++;
      return __bbuddy_block_alloc(b,align/2,m_flag);
    }
  }

 __is_free_long: 
  while(o<32) {
    if((b->nbmp[p_indx+i] & (1 << o)) && !m_flag) { /* yep, found */
      b->nbmp[p_indx+i] &= ~(1 << o); /* used */
      b->pbmp[p_indx+i] |= (1 << o); /* not separated */
      return (p_indx > 1) ? (((p_indx+i)-2) << 5)+o : o;
    } else if((b->nbmp[p_indx+i] & (1 << o)) && m_flag) { /* make sep */
      b->nbmp[p_indx+i] &= ~(1 << o); /* used */
      b->pbmp[p_indx+i] &= ~(1 << o); /* separated */
      /* mark childs free */
      align*=2;	p_indx=align/32; o*=2;
      p_indx=bbuddy_indexp(align,o);
      n_indx=bbuddy_indexbn(o);
      b->nbmp[p_indx] |= (1 << n_indx);
      b->nbmp[p_indx] |= (1 << (n_indx+1));
      b->pbmp[p_indx] |= (1 << n_indx);
      b->pbmp[p_indx] |= (1 << (n_indx+1));
      
      m_flag--;
      return __bbuddy_block_alloc(b,align,m_flag);
    }
    o++;
  }


  return 0;
}

