#ifndef __IPC_BUFFER__
#define  __IPC_BUFFER__

#include <eza/arch/types.h>
#include <eza/task.h>

typedef struct __ipc_buffer_chunk {
  uintptr_t kaddr;
  ulong_t length;
} ipc_buffer_chunk_t;

typedef struct __ipc_buffer {
  uintptr_t start_addr;
  ulong_t length, num_chunks;
  ipc_buffer_chunk_t chunks[];
} ipc_buffer_t;

status_t ipc_create_buffer(task_t *owner,uintptr_t start_addr, ulong_t size);
status_t ipc_destroy_buffer(task_t *owner,uintptr_t start_addr, ulong_t size);

#define IPC_LOCK_BUFFER(b)
#define IPC_UNLOCK_BUFFER(b)

#endif
