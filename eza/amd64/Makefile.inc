CFLAGS+=-m64 -mcmodel=kernel -mno-red-zone -fno-unwind-tables

LIBARCHEZA_OBJS=eza/amd64/pmm.o eza/amd64/e820map.o \
	eza/amd64/boot.o \
	eza/amd64/sched/context.o \
	eza/amd64/asm.o \
	eza/amd64/cpu.o \
	eza/amd64/bios.o \
	eza/amd64/mm/init.o \
	eza/amd64/timer.o \
	eza/amd64/platform.o \
	eza/amd64/interrupt.o \
	eza/amd64/fault.o \
	eza/amd64/mm/mm.o \
	eza/amd64/mm/pt.o \
	eza/amd64/sched/task.o \
	eza/amd64/hw/i8254.o \
	eza/amd64/hw/i8259.o \
	eza/amd64/hw/apic.o \
	eza/amd64/hw/ioapic.o \
	eza/amd64/hw/fake_apic.o \
	eza/amd64/idt.o \
	eza/amd64/syscalls.o \
	eza/amd64/usercopy.o

ifeq ($(CONFIG_SMP),y)
LIBARCHEZA_OBJS += eza/amd64/smpap.o \
	eza/amd64/smp.o \
	eza/amd64/smp_int.o
endif
