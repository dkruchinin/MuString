#ifndef __MSTRING_MMAN_H__
#define __MSTRING_MMAN_H__

#include <mlibc/types.h>

enum mman_prot {
  PROT_NONE    = 0x01,
  PROT_READ    = 0x02,
  PROT_WRITE   = 0x04,
  PROT_EXEC    = 0x08,
  PROT_NOCACHE = 0x10,
};

enum mman_flags {
  MAP_FIXED    = 0x001,
  MAP_ANON     = 0x002,
  MAP_PRIVATE  = 0x004,
  MAP_SHARED   = 0x008,
  MAP_PHYS     = 0x010,
  MAP_STACK    = 0x020,
  MAP_POPULATE = 0x040,
  MAP_GENERIC  = 0x080,
};

typedef unsigned long memobj_id_t;

typedef enum memobj_nature {
  MMO_NTR_GENERIC = 1,
  MMO_NTR_SRV,
  MMO_NTR_PAGECACHE,
  MMO_NTR_PROXY,
} memobj_nature_t;

enum memobj_life {
  MMO_LIFE_SPIRIT = 1,
  MMO_LIFE_STICKY,
  MMO_LIFE_LEECH,
  MMO_LIFE_IMMORTAL,
};

enum {
  MEMOBJ_DPC        = 0x01,
  MEMOBJ_BACKENEDED = 0x02,
  MEMOBJ_NOSHARED   = 0x04,
};

struct memobj_info {
  memobj_id_t id;
  size_t size;
  uid_t owner_uid;
  gid_t owner_gid;
  mode_t acc_mode;
  int users;
  enum memobj_nature nature;
  enum memobj_life lifetype;
  uint32_t flags;
};

#endif /* __MSTRING_MMAN_H__ */
