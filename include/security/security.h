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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/security/security.h: Common data types and function prototypes for
 *                              the kernel security facility.
 */

#ifndef __SECURITY_H__
#define __SECURITY_H__

#include <mstring/types.h>

typedef unsigned int mac_label_t;

struct __s_object {
  mac_label_t mac_label;
};

struct __s_subject {
  mac_label_t mac_label;
};

#define S_MAC_OK(subject_label,object_label) ((subject_label) <= (object_label))

static inline bool s_access_valid(struct __s_subject *subj,struct __s_object *obj)
{
  if( S_MAC_OK(subj->mac_label,obj->mac_label) ) {
    return true;
  }
  return false;
}

#endif
