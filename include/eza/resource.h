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
 * include/eza/resource: base types and prototypes for kernel resource
 *                       management.
 */

#ifndef __RESOURCE_H__
#define __RESOURCE_H__

#include <ds/list.h>
#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/task.h>

typedef enum __resource_type {
  RESOURCE_IO_PORT = 0,
} resource_type_t;

typedef struct __resource {
  atomic_t ref_count;
  list_node_t l;
  resource_type_t type;

  void (*add_ref)(struct __resource *r); 
  void (*release)(struct __resource *r);
} resource_t;

typedef struct __resource_storage {
  list_head_t resources;
} resource_storage_t;

static inline void init_resource(resource_t *resource,resource_type_t type)
{
  resource->type = type;
  atomic_set(&resource->ref_count, 1);
  list_init_node(&resource->l);
  resource->add_ref = resource->release = NULL;
}

void initialize_ioports(void);

status_t arch_allocate_ioports(task_t *task,ulong_t start_port,
                               ulong_t num_ports);
void initialize_resources(void);

#endif

