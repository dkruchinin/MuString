#ifndef __PTD_H__
#define __PTD_H__

#include <eza/arch/types.h>

typedef struct __per_task_data {
  int errno;
  void *task_private_data;
  void *process_shared_data;
} per_task_data_t;

#define PER_TASK_DATA_SIZE  sizeof(per_task_data_t)

#endif
