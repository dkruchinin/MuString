#ifndef __IDT_H__
#define  __IDT_H__

#include <eza/arch/types.h>
#include <eza/interrupt.h>

typedef void (*idt_handler_t)(void);

typedef struct __idt {
  const char *id;
  irq_t (*num_entries)(void);
  irq_t (*vectors_available)(void);
  irq_t (*first_available_vector)(void);
  bool (*is_active_vector)(irq_t vec);
  status_t (*install_handler)(idt_handler_t h, irq_t vec);
  status_t (*free_handler)(irq_t vec);
  void (*initialize)(void);
} idt_t;

const idt_t *get_idt(void);

#endif
