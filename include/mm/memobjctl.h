#ifndef __MSTRING_MEMOBJCTL_H__
#define __MSTRING_MEMOBJCTL_H__

#include <mlibc/types.h>

typedef unsigned long memobj_id_t;

typedef enum memobj_nature {
  MMO_NTR_GENERIC = 1,
  MMO_NTR_SRV,
  MMO_NTR_PCACHE,
  MMO_NTR_PCACHE_BACKED,
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
  MEMOBJ_BACKENDED  = 0x02,
  MEMOBJ_NOSHARED   = 0x04,
  MEMOBJ_SLAVE      = 0x08,
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

struct memobj_backend_info {
  ulong_t port_id;
  pid_t server_pid;
};

enum {
  MREQ_TYPE_GETPAGE  = 1,
  MREQ_TYPE_SYNCPAGE,
};

#define MFAULT_READ  0x01
#define MFAULT_WRITE 0x02
#define MFAULT_NP    0x04

struct memobj_backend_request {
  memobj_id_t memobj_id;
  int type;
  pgoff_t pg_offset;
  ulong_t priv;
  int fault_mask;
};

struct memobj_backend_asnwer {
  int status;
  uintptr_t private;
};

/* Memory object Commands */
enum {  
  MEMOBJ_CTL_TRUNC = 1,
  MEMOBJ_CTL_GET_PAGE,
  MEMOBJ_CTL_PUT_PAGE,
  MEMOBJ_CTL_GET_INFO,
  MEMOBJ_CTL_CHANGE_INFO,
  MEMOBJ_CTL_SET_BACKEND,
  MEMOBJ_CTL_GET_BACKEND,
};

#endif /* __MSTRING_MEMOBJCTL_H__ */
