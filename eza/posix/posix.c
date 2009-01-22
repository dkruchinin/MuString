#include <eza/posix.h>
#include <eza/task.h>

posix_stuff_t *get_task_posix_stuff(task_t *task)
{
  return task->posix_stuff;
}

void release_task_posix_stuff(posix_stuff_t *stuff)
{
}

long posix_allocate_obj_id(posix_stuff_t *stuff)
{
  return 0;
}

void posix_free_obj_id(posix_stuff_t *stuff,long id)
{
}

void posix_insert_object(posix_stuff_t *stuff,posix_kern_obj_t *obj,
                         ulong_t id)
{
}

