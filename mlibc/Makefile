SUP_ARCH := $(shell find $(target)/mach -maxdepth 1 -type d -name $(ARCH))

ifdef $(SUP_ARCH)
	obj-y += mach/$(ARCH)
endif

<<<<<<< HEAD:mlibc/Makefile
obj-y += stdlib.o ctype.o kprintf.o unistd.o index_array.o
=======
obj-y += stdlib.o ctype.o kprintf.o unistd.o index_array.o \
	linked_array.o waitqueue.o rbtree.o index_allocator.o
>>>>>>> zzz:mlibc/Makefile
