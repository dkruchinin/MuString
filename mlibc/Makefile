SUP_ARCH := $(shell find $(target)/mach -maxdepth 1 -type d -name $(ARCH))

ifdef $(SUP_ARCH)
	obj-y += mach/$(ARCH)
endif

obj-y += stdlib.o ctype.o kprintf.o unistd.o index_array.o \
	linked_array.o waitqueue.o rbtree.o
