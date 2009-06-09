#ifndef __MSTRING_MMAN_H__
#define __MSTRING_MMAN_H__

#include <mstring/types.h>

enum mman_prot {
  PROT_NONE    = 0x01,
  PROT_READ    = 0x02,
  PROT_WRITE   = 0x04,
  PROT_EXEC    = 0x08,
  PROT_NOCACHE = 0x10,
};

enum mman_flags {
  MAP_FIXED        = 0x001,
  MAP_ANON         = 0x002,
  MAP_PRIVATE      = 0x004,
  MAP_SHARED       = 0x008,
  MAP_PHYS         = 0x010,
  MAP_STACK        = 0x020,
  MAP_POPULATE     = 0x040,
  MAP_CANRECPAGES  = 0x080,
};

struct mmap_args {
  uintptr_t addr;
  size_t size;
  int prot;
  int flags;
  off_t offset;
};

#endif /* __MSTRING_MMAN_H__ */
