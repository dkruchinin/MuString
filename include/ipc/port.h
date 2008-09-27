#ifndef __IPC_PORT__
#define __IPC_PORT__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/arch_ipc.h>

#define IPC_ACC_READ  0x1
#define IPC_ACC_WRITE  0x2

typedef struct __ipc_port_message_t {
  ulong_t mode,data_size;
  arch_ipc_port_ctx_t *ctx;
} ipc_port_message_t;

typedef struct __ipc_port_t {
} ipc_port_t;

void initialize_ipc(void);

#endif
