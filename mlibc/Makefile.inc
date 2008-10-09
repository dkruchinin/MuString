
ifeq ($(ARCH),amd64)
MLIBC_OBJS+=mlibc/mach/$(ARCH)/memset.o \
	mlibc/mach/$(ARCH)/memcpy.o
endif

MLIBC_OBJS+=mlibc/stdlib.o\
        mlibc/ctype.o \
	mlibc/kprintf.o \
	mlibc/unistd.o \
	mlibc/index_array.o \
	mlibc/linked_array.o \
	mlibc/waitqueue.o \
	mlibc/rbtree.o
