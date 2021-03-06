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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 * (c) Copyright 2010 Jari OS non-profit org. <http://jarios.org>
 * (c) Copyright 2010 MadTirra <madtirra@jarios.org>
 *
 *
 */

/*
 * TODO:
 * This ugly peace of ... code was written for test purpose. After FS
 * design is completed, this crap *must* be rewritten.
 */

#include <config.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/backend.h>
#include <mm/rmap.h>
#include <mm/page_alloc.h>
#include <ipc/channel.h>
#include <ipc/ipc.h>
#include <mstring/types.h>

static int proxy_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  memobj_t *memobj = vmr->memobj;
  struct mmev_fault msg;
  iovec_t snd_iovec, rcv_iovec;
  int ret;
  uintptr_t rcvaddr;
  vmm_t *serv_vmm;
  vmm_t *cli_vmm = vmr->parent_vmm;
  vmrange_t *serv_vmr;
  page_idx_t pidx;
  page_frame_t *page;
  task_t *server = NULL;
  ipc_channel_t *chan = NULL;

  ret = 0;
  spinlock_lock_read(&memobj->members_rwlock);
  if (addr2pgoff(vmr, addr) >= memobj->size) {
    ret = -EFAULT;
  }
  else {
    server = memobj->backend.server;
    if (server)
      grab_task_struct(server);

    chan = memobj->backend.channel;
    if (chan)
      ipc_pin_channel(chan);
  }

  spinlock_unlock_read(&memobj->members_rwlock);
  if (ret)
    return ret;
  if (!server || !chan) {
    if (server)
      release_task_struct(server);
    if (chan)
      ipc_unpin_channel(chan);

    return -ENOENT;
  }

  serv_vmm = server->task_mm;
  msg.hdr.event = MMEV_PAGE_FAULT;
  msg.hdr.private = (long)memobj->private;
  msg.hdr.memobj_id = memobj->id;
  msg.pfmask = pfmask;
  msg.offset = addr2pgoff(vmr, addr);

  snd_iovec.iov_base = (void *)&msg;
  snd_iovec.iov_len = sizeof(msg);
  rcv_iovec.iov_base = (void *)&rcvaddr;
  rcv_iovec.iov_len = sizeof(uintptr_t);

  ret = ipc_port_send_iov(chan, &snd_iovec, 1, &rcv_iovec, 1);
  if (ret < 0) {
    return ret;
  }

  if (rcvaddr & PAGE_MASK) {
    return (int)rcvaddr;
  }

  rwsem_down_read(&serv_vmm->rwsem);
  serv_vmr = vmrange_find(serv_vmm, rcvaddr, rcvaddr + 1, NULL);
  if (!serv_vmr) {
    ret = -EFAULT;
    goto out_unlock_srv;
  }
  if (serv_vmr->memobj != memobj) {
    ret = -EINVAL;
    goto out_unlock_srv;
  }

  RPD_LOCK_READ(&serv_vmm->rpd);
  pidx = vaddr_to_pidx(&serv_vmm->rpd, rcvaddr);
  if (pidx == PAGE_IDX_INVAL) {
    ret = -EFAULT;
    RPD_UNLOCK_READ(&serv_vmm->rpd);
    goto out_unlock_srv;
  }

  page = pframe_by_id(pidx);
  pin_page_frame(page);
  RPD_UNLOCK_READ(&serv_vmm->rpd);
  RPD_LOCK_WRITE(&cli_vmm->rpd);
  ret = mmap_page(&cli_vmm->rpd, addr, pframe_number(page), vmr->flags);
  if (ret) {
    unpin_page_frame(page);
    RPD_UNLOCK_WRITE(&cli_vmm->rpd);
    goto out_unlock_srv;
  }

  ret = rmap_register_mapping(vmr->memobj, page, cli_vmm, addr);
  if (ret) {
    unpin_page_frame(page);
    RPD_UNLOCK_WRITE(&cli_vmm->rpd);
    goto out_unlock_srv;
  }

  RPD_UNLOCK_WRITE(&cli_vmm->rpd);

out_unlock_srv:
  rwsem_up_read(&serv_vmm->rwsem);
  release_task_struct(server);
  ipc_unpin_channel(chan);
  return ret;
}

static int proxy_depopulate_pages(vmrange_t *vmr, uintptr_t va_from, uintptr_t va_to)
{
  memobj_t *memobj = vmr->memobj;
  vmm_t *vmm = vmr->parent_vmm;
  page_idx_t pidx;
  page_frame_t *page;
  iovec_t snd_iovec, rcv_iovec;
  struct mmev_mdep msync;
  int ret, srvret;
  task_t *server;
  ipc_channel_t *chan;

  ASSERT(memobj->backend.server != NULL);
  spinlock_lock_read(&memobj->members_rwlock);
  server = memobj->backend.server;
  grab_task_struct(server);
  chan = memobj->backend.channel;
  ipc_pin_channel(chan);
  spinlock_unlock_read(&memobj->members_rwlock);

  while (va_from < va_to) {
    RPD_LOCK_WRITE(&vmm->rpd);
    pidx = vaddr_to_pidx(&vmm->rpd, va_from);
    if (pidx == PAGE_IDX_INVAL) {
      RPD_UNLOCK_WRITE(&vmm->rpd);
      goto eof_cycle;
    }

    page = pframe_by_id(pidx);
    munmap_page(&vmm->rpd, va_from);
    rmap_unregister_mapping(page, vmm, va_from);
    RPD_UNLOCK_WRITE(&vmm->rpd);
    if (vmm != server->task_mm) {
      msync.hdr.event = MMEV_DEPOPULATE;
      msync.hdr.memobj_id = memobj->id;
      msync.hdr.private = (long)memobj->private;
      msync.offset = page->offset;
      msync.size = PAGE_SIZE;

      snd_iovec.iov_base = (void *)&msync;
      snd_iovec.iov_len = sizeof(msync);
      rcv_iovec.iov_base = (void *)&srvret;
      rcv_iovec.iov_len = sizeof(int);

      ret = ipc_port_send_iov(chan, &snd_iovec, 1, &rcv_iovec, 1);
      unpin_page_frame(page);
      if (ret < 0)
        goto eof_cycle;
    }

eof_cycle:
    va_from += PAGE_SIZE;
  }

  release_task_struct(server);
  ipc_unpin_channel(chan);
  return 0;
}

static int proxy_msync(struct __vmrange *vmr, uintptr_t offset,
                       ulong_t pages, int flags)
{
  memobj_t *memobj = vmr->memobj;
  vmm_t *vmm = vmr->parent_vmm;
  page_idx_t pidx;
  page_frame_t *page;
  iovec_t snd_iovec, rcv_iovec;
  struct mmev_msync msync;
  int ret = 0, srvret;
  task_t *server;
  ipc_channel_t *chan;

  ASSERT(memobj->backend.server != NULL);
  spinlock_lock_read(&memobj->members_rwlock);
  server = memobj->backend.server;

  if (vmm != server->task_mm) {
    grab_task_struct(server);
    chan = memobj->backend.channel;
    ipc_pin_channel(chan);
    spinlock_unlock_read(&memobj->members_rwlock);

    RPD_LOCK_WRITE(&vmm->rpd);
    pidx = vaddr_to_pidx(&vmm->rpd, offset);
    if (pidx == PAGE_IDX_INVAL) {
      RPD_UNLOCK_WRITE(&vmm->rpd);
      ret = -EINVAL;
      goto end;
    }

    page = pframe_by_id(pidx);
    RPD_UNLOCK_WRITE(&vmm->rpd);

    msync.hdr.event = MMEV_MSYNC;
    msync.hdr.memobj_id = memobj->id;
    msync.hdr.private = (long)memobj->private;
    msync.offset = page->offset;
    msync.size = PAGE_SIZE;

    snd_iovec.iov_base = (void *)&msync;
    snd_iovec.iov_len = sizeof(msync);
    rcv_iovec.iov_base = (void *)&srvret;
    rcv_iovec.iov_len = sizeof(int);

    ret = ipc_port_send_iov(chan, &snd_iovec, 1, &rcv_iovec, 1);
    unpin_page_frame(page);
    if(!ret && srvret) ret = srvret;

  end:
    release_task_struct(server);
    ipc_unpin_channel(chan);
  } else
    spinlock_unlock_read(&memobj->members_rwlock);

  return ret;
}

static int proxy_mmap_pages_check(struct __memobj *memobj, ulong_t sec_id[5], uintptr_t offset,
                                  ulong_t npages, int proto)
{
  iovec_t snd_iovec, rcv_iovec;
  struct mmev_mmap mmap;
  int ret = 0, srvret;
  task_t *server;
  ipc_channel_t *chan;

  ASSERT(memobj->backend.server != NULL);
  spinlock_lock_read(&memobj->members_rwlock);
  server = memobj->backend.server;
  grab_task_struct(server);
  chan = memobj->backend.channel;
  ipc_pin_channel(chan);
  spinlock_unlock_read(&memobj->members_rwlock);

  /* we're sending first page and size on pages to allocate */
  mmap.hdr.event = MMEV_MMAP_CHECK;
  mmap.hdr.memobj_id = memobj->id;
  mmap.hdr.private = (long)memobj->private;
  mmap.offset = offset;
  mmap.size = npages;

  snd_iovec.iov_base = (void *)&mmap;
  snd_iovec.iov_len = sizeof(mmap);
  rcv_iovec.iov_base = (void *)&srvret;
  rcv_iovec.iov_len = sizeof(int);

  ret = ipc_port_send_iov(chan, &snd_iovec, 1, &rcv_iovec, 1);
  if(!ret && srvret) ret = srvret;

  release_task_struct(server);
  ipc_unpin_channel(chan);
  return ret;
}

static int proxy_unmap_ack(struct __memobj *memobj)
{
  iovec_t snd_iovec, rcv_iovec;
  struct mmev_hdr unmap_ack;
  int ret = 0, srvret;
  task_t *server;
  ipc_channel_t *chan;

  ASSERT(memobj->backend.server != NULL);
  spinlock_lock_read(&memobj->members_rwlock);
  server = memobj->backend.server;
  grab_task_struct(server);
  chan = memobj->backend.channel;
  ipc_pin_channel(chan);
  spinlock_unlock_read(&memobj->members_rwlock);

  unmap_ack.event = MMEV_MUNMAP;
  unmap_ack.memobj_id = memobj->id;
  unmap_ack.private = (long)memobj->private;

  snd_iovec.iov_base = (void *)&unmap_ack;
  snd_iovec.iov_len = sizeof(unmap_ack);
  rcv_iovec.iov_base = (void *)&srvret;
  rcv_iovec.iov_len = sizeof(int);

  ret = ipc_port_send_iov(chan, &snd_iovec, 1, &rcv_iovec, 1);
  if(!ret && srvret) ret = srvret;

  release_task_struct(server);
  ipc_unpin_channel(chan);
  return ret;
}

static int proxy_populate_pages(vmrange_t *vmr, uintptr_t addr, page_idx_t npages)
{
  memobj_t *memobj = vmr->memobj;
  vmm_t *vmm = vmr->parent_vmm;
  list_head_t h;
  list_node_t *n, *safe;
  page_frame_t *pages, *p;
  int ret = 0;

  if (!memobj->backend.server) {
    return -ENOENT;
  }
  if (vmr->parent_vmm != memobj->backend.server->task_mm)
    return -EINVAL;

  pages = alloc_pages(npages, MMPOOL_USER | AF_ZERO);
  if (!pages) {
    ret = -EFAULT;
    goto out;
  }

  list_init_head(&h);
  list_set_head(&h, &pages->chain_node);
  RPD_LOCK_WRITE(&vmm->rpd);
  list_for_each_safe(&h, n, safe) {
    p = list_entry(n, page_frame_t, chain_node);
    list_del(n);
    p->flags |= PF_SHARED;
    pin_page_frame(p);
    ret = mmap_page(&vmm->rpd, addr, pframe_number(p), vmr->flags);
    if (ret) {
      unpin_page_frame(p);
      RPD_UNLOCK_WRITE(&vmm->rpd);
      goto out;
    }

    ret = rmap_register_mapping(memobj, p, vmm, addr);
    if (ret) {
      unpin_page_frame(p);
      RPD_UNLOCK_WRITE(&vmm->rpd);
      goto out;
    }

    p->offset = addr2pgoff(vmr, addr);
    addr += PAGE_SIZE;
  }

  RPD_UNLOCK_WRITE(&vmm->rpd);
out:
  return ret;
}

static int proxy_truncate(memobj_t *memobj, pgoff_t new_size)
{
  spinlock_lock_write(&memobj->members_rwlock);
  memobj->size = new_size;
  spinlock_unlock_write(&memobj->members_rwlock);

  return 0;
}

static memobj_ops_t proxy_ops = {
  .handle_page_fault = proxy_page_fault,
  .populate_pages = proxy_populate_pages,
  .depopulate_pages = proxy_depopulate_pages,
  .cleanup = NULL,
  .insert_page = NULL,
  .delete_page = NULL,
  .mmap_check = proxy_mmap_pages_check,
  .unmap_ack = proxy_unmap_ack,
  .truncate = proxy_truncate,
  .msync = proxy_msync,
};

int proxy_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  atomic_set(&memobj->users_count, 1);
  memobj->mops = &proxy_ops;
  memobj->flags = flags;
  return 0;
}
