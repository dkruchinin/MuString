#include <config.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/slab.h>
#include <mm/rmap.h>
#include <mlibc/assert.h>
#include <mlibc/types.h>

static memcache_t *heads_cache, *entries_cache;


void rmap_subsystem_initialize(void)
{
  char *names[2] = { "RMAP heads", "RMAP entries" };
  size_t sizes[2] = { sizeof(rmap_group_head_t), sizeof(rmap_group_entry_t) };
  memcache_t *caches[2];
  int i;

  for (i =0; i < 2; i++) {
    caches[i] = create_memcache(names[i], sizes[i], 1, SMCF_PGEN | SMCF_GENERIC);
    if (!caches[i]) {
      panic("Failed to create memory cache for \"%s\" (size = %zd). ENOMEM\n",
            names[i], sizes[i]);
    }
  }

  heads_cache = caches[0];
  entries_cache = caches[1];
  kprintf("[MM] Reverse mapping system successfully initialized\n");
}

static rmap_group_entry_t *create_new_entry(vmm_t *vmm, uintptr_t addr)
{
  rmap_group_entry_t *entry = alloc_from_memcache(entries_cache);

  if (!entry)
    return NULL;

  entry->vmm = vmm;
  entry->addr = addr;
  list_init_node(&entry->node);

  return entry;
}

static rmap_group_head_t *create_new_head(memobj_t *memobj)
{
  rmap_group_head_t *group_head = alloc_from_memcache(heads_cache);

  if (!group_head)
    return NULL;

  list_init_head(&group_head->head);
  group_head->memobj = memobj;
  group_head->num_locations = 0;

  return group_head;
}

int rmap_register_anon(page_frame_t *page, vmm_t *vmm, uintptr_t addr)
{
  rmap_group_entry_t *entry;

  ASSERT_DBG(!(page->flags & (PF_SHARED | PF_COW)));
  entry = page->rmap_anon;
  ASSERT_DBG(entry == NULL);
  entry = create_new_entry(vmm, addr);
  if (!entry)
    return -ENOMEM;

  entry->memobj = generic_memobj;
  page->rmap_anon = entry;
  return 0;
}

int rmap_register_shared_entry(page_frame_t *page, vmm_t *vmm, uintptr_t addr)
{
  rmap_group_head_t *group_head = page->rmap_shared;
  rmap_group_entry_t *ge;

  ASSERT_DBG(page->flags & (PF_SHARED | PF_COW));
  if (unlikely(group_head == NULL))
    return -EINVAL;

  ge = create_new_entry(vmm, addr);
  if (!ge)
    return -ENOMEM;

  list_add2tail(&group_head->head, &ge->node);
  group_head->num_locations++;
  return 0;
}

int rmap_register_shared(memobj_t *memobj, page_frame_t *page, vmm_t *vmm, uintptr_t addr)
{
  rmap_group_head_t *group_head = page->rmap_shared;
  int ret = 0;

  ASSERT_DBG(page->flags & (PF_SHARED | PF_COW));
  if (!group_head) {
    group_head = create_new_head(memobj);
    if (!group_head)
      return -ENOMEM;
  }

  page->rmap_shared = group_head;
  ret = rmap_register_shared_entry(page, vmm, addr);
  if (ret) {
    memfree(group_head);
    page->rmap_shared = NULL;
  }

  return ret;
}

int rmap_register_mapping(memobj_t *memobj, page_frame_t *page, vmm_t *vmm, uintptr_t address)
{
  int ret;
  
  lock_page_frame(page, PF_LOCK);
  if (!(page->flags & (PF_SHARED | PF_COW))) {
    rmap_group_entry_t *entry = page->rmap_anon;

    if (unlikely(entry != NULL)) {
      kprintf(KO_ERROR "Trying to register reverse anonymous mapping for page(%#x) that "
              "already has one: (VMM's pid = %ld, addr = %p). Register: "
              "(VMM's pid = %ld, addr = %p)!\n", pframe_number(page),
              entry->vmm->owner->pid, entry->addr, vmm->owner->pid, address);
      ret = -EALREADY;
      goto out;
    }

    ret = rmap_register_anon(page, vmm, address);
  }
  else {
    ret = rmap_register_shared(memobj, page, vmm, address);
  }

out:
  unlock_page_frame(page, PF_LOCK);
  return ret;
}

int rmap_unregister_shared(page_frame_t *page, vmm_t *vmm, uintptr_t addr)
{
  rmap_group_head_t *group_head = page->rmap_shared;
  rmap_group_entry_t *entry = NULL;
  list_node_t *n, *s;
  bool found = false;

  if (!group_head) {
    kprintf(KO_WARNING "Trying to unregister shared mapping for page %#x. "
            "The page hasn't associated with it rmap_shared structure\n",
            pframe_number(page));
    return -EINVAL;
  }
  list_for_each_safe(&group_head->head, n, s) {
    entry = list_entry(n, rmap_group_entry_t, node);
    if ((entry->vmm == vmm) && (entry->addr == addr)) {
      found = true;
      list_del(&entry->node);
      memfree(entry);
      group_head->num_locations--;
    }
  }
  if (!found) {
    kprintf(KO_WARNING "Trying to unregister shared mapping for page %#x. "
            "Mapping with VMM(pid = %ld) and address %p wasn't found!\n",
            vmm->owner->pid, addr);
    return -ESRCH;
  }
  if (!group_head->num_locations) {
    memfree(group_head);
    page->rmap_shared = NULL;
  }

  return 0;
}

int rmap_unregister_anon(page_frame_t *page, vmm_t *vmm, uintptr_t addr)
{
  rmap_group_entry_t *rmap_anon = page->rmap_anon;

  ASSERT_DBG(!(page->flags & (PF_SHARED | PF_COW)));
  ASSERT_DBG(rmap_anon != NULL);
  if (rmap_anon->vmm != vmm) {
    kprintf(KO_WARNING "Trying to unregister invalid anonymous mapping for page %#x. "
            "VMM's mismatch: (Existing VMM's pid = %ld, given VMM's pid = %ld)\n",
            pframe_number(page), rmap_anon->vmm->owner->pid, vmm->owner->pid);
    return -ESRCH;
  }
  if (addr != rmap_anon->addr) {
    kprintf(KO_WARNING "Trying to unregister invalid anonymous mapping for page %#x. "
            "Addresses mismatch: Existing = %p, Given = %p\n", rmap_anon->addr, addr);
    return -ESRCH;
  }

  memfree(rmap_anon);
  page->rmap_anon = NULL;

  return 0;
}

int rmap_unregister_mapping(page_frame_t *page, vmm_t *vmm, uintptr_t address)
{
  int ret;
  
  lock_page_frame(page, PF_LOCK);
  if (!(page->flags & (PF_SHARED | PF_COW))) {
    if (unlikely(!page->rmap_anon)) {
      kprintf(KO_WARNING "Can not unregister anonymous mapping (VMM's pid = %ld, addr = %p) "
              "for page #%x, because it hasn't \"rmap_anon\" field set.\n",
              vmm->owner->pid, address, pframe_number(page));
      goto out;
    }

    ret = rmap_unregister_anon(page, vmm, address);
  }
  else {
    ret = rmap_unregister_shared(page, vmm, address);
  }

out:
  unlock_page_frame(page, PF_LOCK);
  return ret;
}
