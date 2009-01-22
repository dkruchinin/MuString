SUP_ARCH :=

ifdef $(ARCH)
SUP_ARCH := $(shell find $(target)/mach -maxdepth 1 -type d -name $(ARCH))
ifdef $(SUP_ARCH)
	obj-y += mach/$(ARCH)
endif
endif

obj-y += stdlib.o ctype.o kprintf.o unistd.o index_array.o 
