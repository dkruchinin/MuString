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
 * (c) Copyright 2006-2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 * @date 2008
 * @file include/eza/milestones.h
 * "Milestones" that are used in the kernel for debug purpose.
 * Particular pointer may be marked with particular milestone. If anywhere in the kernel
 * someone will try to dereference marked by milestone pointer, page fault will occur and
 * display an address where it has heppend.  
 *
 */

/**
 * @file include/eza/milestones.h
 * "Milestones" that are used in the kernel for debug purpose.
 * Milestones are used to mark particular pointer of particular "class"
 * as "invalid". If then someone will try to dereference marked by milestone
 * pointer, milestone's base(equals to __MILESTONE macro) + milestone's number will be
 * displayed by page fault. Using these information you can determine what "class"
 * caued that situation.
 * For example there are two milestones used in lists: MLST_LIST_NEXT and MLST_LIST_PREV.
 * After list node has been deleted, its next and prev are marked by MLST_LIST_NEXT
 * and MLST_LIST_PREV accordingly. If someone will try to jump by either next or prev of
 * removed list node, page fault will occur.
 * We'll be able to determine that page fault was caused by operations with removed list node
 */

#ifndef __MILESTONES_H__
#define __MILESTONES_H__

/* Page fault heppens if anyone tries to jump to this address */
#define __MILESTONE ((unsigned long)0x123)

/**
 * @def MILESTONES_SEQ(num) ((void *)__MILESTONE##num)
 * @brief Easy way to define next item in milestones sequence
 * @pararm num - number of milestone in sequence
 */
#define MILESTONES_SEQ(num) ((void *)(__MILESTONE + (num)))

/* milestones for list's next and prev pointers respectively */
#define MLST_LIST_NEXT MILESTONES_SEQ(1)
#define MLST_LIST_PREV MILESTONES_SEQ(2)

#endif /* __MILESTONES_H__ */
