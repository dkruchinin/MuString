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
 * include/mm/page.h: constants used for page manipulation
 *
 */

#ifndef __MM_PAGE_H__
#define __MM_PAGE_H__

#define PAGE_CACHEABLE_SHIFT      0
#define PAGE_NOT_CACHEABLE_SHIFT  PAGE_CACHEABLE_SHIFT
#define PAGE_PRESENT_SHIFT        1
#define PAGE_NOT_PRESENT_SHIFT    PAGE_PRESENT_SHIFT
#define PAGE_USER_SHIFT           2
#define PAGE_KERNEL_SHIFT         PAGE_USER_SHIFT
#define PAGE_READ_SHIFT           3
#define PAGE_WRITE_SHIFT          4
#define PAGE_EXEC_SHIFT           5
#define PAGE_GLOBAL_SHIFT         6

#define PAGE_NOT_CACHEABLE  (0 << PAGE_CACHEABLE_SHIFT)
#define PAGE_CACHEABLE      (1 << PAGE_CACHEABLE_SHIFT)

#define PAGE_PRESENT      (0 << PAGE_PRESENT_SHIFT)
#define PAGE_NOT_PRESENT  (1 << PAGE_PRESENT_SHIFT)

#define PAGE_USER    (1 << PAGE_USER_SHIFT)
#define PAGE_KERNEL  (0 << PAGE_USER_SHIFT)

#define PAGE_READ   (1 << PAGE_READ_SHIFT)
#define PAGE_WRITE  (1 << PAGE_WRITE_SHIFT)
#define PAGE_EXEC   (1 << PAGE_EXEC_SHIFT)

#define PAGE_GLOBAL  (1 << PAGE_GLOBAL_SHIFT)

#endif /* __MM_PAGE_H__ */

