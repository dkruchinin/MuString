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
 * include/mlibc/varargs.h: va_list standart macros
 *
 */

#ifndef __VARARGS_H__
#define __VARARGS_H__

typedef char* va_list;

#define _va_size(type) (((sizeof(type) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))
#define va_start(var_list, arg) (var_list = ((va_list) &(arg) + _va_size(arg)))
#define va_end(var_list)
#define va_arg(var_list, type)  \
        (var_list += _va_size(type),\
        *((type*)(var_list - _va_size(type))))

#endif /* __VARARGS_H__ */
