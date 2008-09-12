#include <eza/arch/types.h>
#include <eza/resource.h>
#include <mm/pt.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>

/*
static bool def_is_smp(void);
static void def_add_cpu(cpu_id_t cpu);
static cpu_array_t scheduler_tick(void);
static void add_task(task_t *task);
static void schedule(void);
static task_t *get_running_task(cpu_id_t cpu); 
*/
/*

static scheduler_t eza_default_scheduler {
  const char *id;
  list_head_t l;
  bool (*is_smp)(void);
  void (*add_cpu)(cpu_id_t cpu);
  cpu_array_t (*scheduler_tick)(void);
  void (*add_task)(task_t *task);
  void (*schedule)(void);
  task_t *(*get_running_task)(cpu_id_t cpu); 
};
*/
