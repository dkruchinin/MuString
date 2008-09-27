#ifndef __AMD64_ARCH_IPC__
#define  __AMD64_ARCH_IPC__

#include <eza/arch/types.h>

#define ARCH_IPC_PORT_64_BIT_REGS 14
#define ARCH_IPC_PORT_128_BIT_REGS 16
#define ARCH_IPC_PORT_128_BIT_SIZE (ARCH_IPC_PORT_128_BIT_REGS*16)

typedef struct __arch_ipc_port_ctx {
  uint64_t regs64[ARCH_IPC_PORT_64_BIT_REGS];
  char regs128[ARCH_IPC_PORT_128_BIT_SIZE];
} arch_ipc_port_ctx_t;

#endif
