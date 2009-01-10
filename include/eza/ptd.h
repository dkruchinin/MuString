#ifndef __PTD_H__
#define __PTD_H__

#include <eza/arch/types.h>

#define PTD_CUSTOM_SLOTS  8

typedef struct __per_task_data {
  int errno;
  void *process_shared_data;
  void *task_private_data;
  ulong_t custom_slots[PTD_CUSTOM_SLOTS];
} per_task_data_t;

#define PER_TASK_DATA_SIZE  sizeof(per_task_data_t)

#endif
