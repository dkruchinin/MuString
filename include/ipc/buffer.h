#ifndef __IPC_BUFFER__
#define  __IPC_BUFFER__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/arch/atomic.h>

typedef struct __ipc_user_buffer {
  ulong_t length,num_chunks,first;
  uintptr_t *chunks;
} ipc_user_buffer_t;

struct __iovec;
status_t ipc_setup_buffer_pages(task_t *owner,struct __iovec *iovecs,ulong_t numvecs,
                                uintptr_t *addr_array,ipc_user_buffer_t *bufs);
status_t ipc_transfer_buffer_data(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                  void *user_addr,ulong_t to_copy,bool to_buffer);
status_t ipc_transfer_buffer_data_iov(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                      struct __iovec *iovecs,ulong_t numvecs,
                                      bool to_buffer);
#endif
