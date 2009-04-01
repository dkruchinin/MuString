#ifndef __BACKEND_H__
#define __BACKEND_H__

#include <config.h>
#include <ipc/channel.h>
#include <mlibc/types.h>

typedef enum __mm_event {
  MMEV_PAGE_FAULT   = 0x01,
  MMEV_ALLOC_PAGE   = 0x02,
  MMEV_FREE_PAGE    = 0x04,
  MMEV_MSYNC        = 0x08,
  MMEV_TRUNCATE     = 0x10,
} mm_event_t;

typedef struct __memobj_backend_t {
  task_t *server;
  ipc_channel_t *channel;
  mm_event_t events_mask;
} memobj_backend_t;

struct mmev_hdr {
  mm_event_t event;
  memobj_id_t memobj_id;
};

struct mmev_fault {
  struct mmev_hdr hdr;
  uint32_t pfmask;
  pgoff_t offset;
};

struct mmev_msync {
  struct mmev_hdr hdr;
  pgoff_t offset;
};

#endif /* __BACKEND_H__ */
