obj-y += kconsole.o vga.o syscall.o interrupt.o scheduler.o timer.o time.o \
	hardware.o swks.o task.o kstack.o idletask.o panic.o process.o security.o vm.o \
	limits.o uinterrupts.o def_actions.o gc.o exit.o main.o tevent.o signal.o waitqueue.o

ifeq ($(CONFIG_EMU),y)
	obj-y += actbl_emu.o
else
	obj-y += actbl.o
endif

obj-y += schedulers resources
