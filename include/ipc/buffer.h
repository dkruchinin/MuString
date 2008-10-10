#ifndef __IPC_BUFFER__
#define  __IPC_BUFFER__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/arch/atomic.h>

typedef struct __ipc_user_buffer_chunk {
  void *kaddr;
} ipc_user_buffer_chunk_t;

typedef struct __ipc_user_buffer {
  atomic_t use_count;
  ulong_t length,num_chunks,first;
  ipc_user_buffer_chunk_t *chunks;
} ipc_user_buffer_t;

status_t ipc_create_buffer(task_t *owner,uintptr_t start_addr, ulong_t size);
status_t ipc_destroy_buffer(task_t *owner,ulong_t id);
status_t ipc_transfer_buffer_data(ipc_user_buffer_t *buf,ulong_t buf_offset,
                                  ulong_t to_copy, void *user_addr,bool to_buffer);
ipc_user_buffer_t *ipc_get_buffer(task_t *owner,ulong_t buf_id);
status_t ipc_setup_buffer_pages(task_t *owner,ipc_user_buffer_t *buf,
                                uintptr_t start_addr, ulong_t size);

#define IPC_LOCK_BUFFER(b)
#define IPC_UNLOCK_BUFFER(b)

#endif
