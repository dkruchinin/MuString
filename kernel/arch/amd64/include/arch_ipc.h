#ifndef __MSTRING_ARCH_IPC_H__
#define __MSTRING_ARCH_IPC_H__

#include <mstring/types.h>

#define ARCH_IPC_PORT_64_BIT_REGS 14
#define ARCH_IPC_PORT_128_BIT_REGS 16
#define ARCH_IPC_PORT_128_BIT_SIZE (ARCH_IPC_PORT_128_BIT_REGS*16)

typedef struct __arch_ipc_port_ctx {
  uint64_t regs64[ARCH_IPC_PORT_64_BIT_REGS];
  char regs128[ARCH_IPC_PORT_128_BIT_SIZE];
} arch_ipc_port_ctx_t;

#endif /* __MSTRING_ARCH_IPC_H__ */
