#include <eza/arch/types.h>
#include <eza/interrupt.h>
#include <eza/arch/interrupt.h>
#include <eza/idt.h>
#include <eza/arch/asm.h>
#include <eza/spinlock.h>
#include <eza/errno.h>

#define AMD64_IDT_ENTRIES 256

static spinlock_t amd64_idt_lock;

#define LOCK_AMD_IDT spinlock_lock(&amd64_idt_lock)
#define UNLOCK_AMD_IDT spinlock_unlock(&amd64_idt_lock)

typedef struct __amd64_idt_entry {
  bool available;
} amd64_idt_entry_t;

static struct __s {
  amd64_idt_entry_t idt_entries[AMD64_IDT_ENTRIES];
  irq_t available_vectors;
} amd64_idt_table;

static irq_t __num_entries(void)
{
    return AMD64_IDT_ENTRIES;
}

static irq_t __vectors_available(void)
{
  return amd64_idt_table.available_vectors;
}

static irq_t __first_available_vector(void)
{
  return IRQ_BASE;
}

static bool __is_active_vector(irq_t vec)
{
  if(vec < AMD64_IDT_ENTRIES) {
    return amd64_idt_table.idt_entries[vec].available;
  }
  return true;
}

static status_t __install_handler(idt_handler_t h, irq_t vec)
{
  status_t r;
  amd64_idt_entry_t *e;

  if(h == NULL || vec >= AMD64_IDT_ENTRIES || vec < IRQ_BASE) {
    return -EINVAL;
  }

  LOCK_AMD_IDT;
  e = &amd64_idt_table.idt_entries[vec]; 
  r = install_interrupt_gate(vec,(uintptr_t)h,0,0);
  if(r == 0) {
    e->available = false;
  } else {
    e->available = true;
  }
  UNLOCK_AMD_IDT;
  return r;
}

static status_t __free_handler(irq_t vec)
{
  return -EAGAIN;
}

static void __initialize(void)
{
  int i;
  amd64_idt_entry_t *e;
  irq_t avail;

  spinlock_initialize(&amd64_idt_lock, "AMD64 IDT table lock");

  avail = 0;
  for(i=0,e=amd64_idt_table.idt_entries;i<AMD64_IDT_ENTRIES;i++,e++) {
    if( i < IRQ_BASE ) {
      e->available = false;
    } else {
      e->available = true;
      avail++;
    }
  }
  amd64_idt_table.available_vectors = avail;
}

static idt_t amd64_idt = {
  .id = "AMD64 IDT",
  .num_entries = __num_entries,
  .vectors_available = __vectors_available,
  .first_available_vector = __first_available_vector,
  .is_active_vector = __is_active_vector,
  .install_handler = __install_handler,
  .free_handler = __free_handler,
  .initialize = __initialize,
};

const idt_t *get_idt(void)
{
    return &amd64_idt;
}
