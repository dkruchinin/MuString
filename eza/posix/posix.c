#include <eza/posix.h>
#include <eza/task.h>
#include <mm/mm.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <eza/arch/atomic.h>
#include <eza/mutex.h>

posix_stuff_t *get_task_posix_stuff(task_t *task)
{
  return task->posix_stuff;
}

void release_task_posix_stuff(posix_stuff_t *stuff)
{
}

void posix_free_obj_id(posix_stuff_t *stuff,long id)
{
}

posix_stuff_t *allocate_task_posix_stuff(void)
{
  posix_stuff_t *stuff=memalloc(sizeof(*stuff));

  if( stuff ) {
    int i;

    atomic_set(&stuff->use_counter,1);

    for(i=0;i<CONFIG_POSIX_HASH_GROUPS;i++) {
      list_init_head(&stuff->list_hash[i]);
    }
    stuff->timers=0;
    idx_allocator_initialize(&stuff->posix_ids,CONFIG_POSIX_MAX_OBJECTS);
  }

  return stuff;
}
