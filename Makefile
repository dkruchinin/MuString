KERNEL_NAME       := Quoppa
KERNEL_MAJOR_VER  := 0
KERNEL_MIDDLE_VER := 0
KERNEL_MINOR_VER  := 1

ifndef $(BUILD_ROOT)
	BUILD_ROOT := $(CURDIR)
endif

KERNELVERSION := $(KERNEL_MAJOR_VER).$(KERNEL_MIDDLE_VER).$(KERNEL_MINOR_VER)
VERFILE := include/version.h
OBJECTS :=
CC := $(TOOLCHAIN)gcc
AR := $(TOOLCHAIN)ar
AS := $(TOOLCHAIN)as
LD := $(TOOLCHAIN)ld
OBJDUMP := $(TOOLCHAIN)objdump
OBJCOPY := $(TOOLCHAIN)objcopy

HOSTCC := gcc
HOSTLD := ld

-include .config

ifdef CONFIG_ARCH
ARCH := $(shell echo $(CONFIG_ARCH) | sed 's|"||g')
endif

ODIR := mstring_dumps
SCRIPTS_DIR := $(BUILD_ROOT)/scripts
ARCH_DIR := $(BUILD_ROOT)/kernel/arch/$(ARCH)
ARCH_COM_DIR := $(BUILD_ROOT)/kernel/arch/common

GREP := grep
GMAP := $(SCRIPTS_DIR)/gmap.py
MKLINKS := $(SCRIPTS_DIR)/mklinks.sh
LN := ln
RM := rm
CP := cp
MKDIR := mkdir
ECHO := /bin/echo

ifeq ($(VERBOSE),y)
Q := 
MAKE := make
else
Q := @
MAKE := make -s
endif


ifneq ($(NOCOLOR), y)
NOCOLOR :=
endif

OPTIMIZATION ?= -g
CFLAGS = -Wall -nostdlib -nostdinc -fno-builtin -fomit-frame-pointer \
	$(OPTIMIZATION) $(KERN_CFLAGS)

LDFLAGS += -M
INCLUDE += -Iinclude

export CC LD AR OBJDUMP OBJCOPY GMAP GREP CPP AS ECHO
export HOSTCC HOSTLD HOSTCFLAGS HOSTLDFLAGS
export GREP MAKE LN RM GMAP MKDIR CP
export CFLAGS LDFLAGS INCLUDE
export BUILD_ROOT ARCH ARCH_DIR ARCH_COM_DIR OBJECTS
export KERNELVERSION

include include/Makefile.inc

GENERICS = kernel server
ifeq ($(CONFIG_TEST),y)
GENERICS += tests
endif

all: host vmuielf

-include kernel/arch/$(ARCH)/Makefile.inc

host:
	$(call echo-header,"kbuild")
	$(Q)$(MAKE) all -C kbuild BUILD_ROOT=$(BUILD_ROOT)

vmuielf: muielf
	$(call echo-label,"OBJCOPY",$< -> $@)
	$(Q)$(OBJCOPY) -O binary $< $@

muielf: mkbins collect_objects $(ODIR)/kernel.ld
	$(call echo-header,"$@")
	$(call echo-action,"LD",$@)
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) -q $(OBJECTS) -o $@ -Map $(ODIR)/muielf.map
	$(Q)$(OBJDUMP) -t $(OBJECTS) > $(ODIR)/muielf.objdump

mkbins: check_config prepare $(addprefix generic_, $(GENERICS))

prepare: include/arch
	$(Q)$(MKDIR) -p $(ODIR)
	$(Q)$(MKLINKS) clear
	$(Q)$(MKLINKS) create

include/arch:
	$(Q)$(LN) -s $(BUILD_ROOT)/kernel/arch/$(ARCH)/include $(BUILD_ROOT)/include/arch

check_config:
ifeq ($(shell [ -f $(BUILD_ROOT)/.config ] && echo "y"),)
	$(Q)$(MAKE) help_config
endif

generic_%:
	$(call echo-header,"$(subst generic_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst generic_,,$@)

clean_%:
	$(call echo-header,"Cleaning $(subst clean_,,$@):")
	$(Q)$(MAKE) -f rules.mak target=$(subst clean_,,$@) clean

clean:
	$(Q)$(RM) -rf $(ODIR)
	$(Q)$(RM) -f muielf vmuielf boot.img $(BUILD_ROOT)/include/arch
	$(Q)$(MKLINKS) clear
	$(Q)$(MAKE) -C. $(addprefix clean_, $(GENERICS))

clean_host:
	$(call echo-header,"Cleaning host")
	$(Q)$(MAKE) -C kbuild clean

cleanconf:
	$(call echo-header,"Cleaning configs")
	$(Q)$(RM) -rf .config include/config.h include/config/ $(VERFILE)

distclean: clean_host clean cleanconf

$(ODIR)/rmap.o: $(ODIR)/rmap.bin
	$(call create_rmap)

collect_objects:
	$(eval OBJECTS := $(call collect_objects))

image: vmuielf
ifneq ($(NOBUILDIMG),y) 
# Creates a 20 MB bootable FAT HD image: 44 tracks, 16 heads, 63 sectors
	@dd if=/dev/zero of=boot.img count=44352 bs=512
	@echo "drive c: file=\"`pwd`/boot.img\" partition=1" > ~/.mtoolsrc
	@mpartition -I -s 63 -t 44 -h 16 c:
	@mpartition -cpv -s 63 -t 44 -h 16 c:
	@mformat c:
	@mmd c:/boot
	@mmd c:/boot/grub
	@mcopy /boot/grub/stage1 c:/boot/grub
	@mcopy /boot/grub/stage2 c:/boot/grub
	@mcopy /boot/grub/fat_stage1_5 c:/boot/grub
	@echo "(hd0) boot.img" > bmap
	@printf "geometry (hd0) 44 16 63\nroot (hd0,0)\nsetup (hd0)\n" | /usr/sbin/grub --batch --device-map=bmap
	@printf "default 0\ntimeout=2\ntitle=MString kernel\nkernel=/boot/grub/kernel.bin" > ./menu.lst
	@mcopy ./menu.lst c:/boot/grub
	@mcopy ./vmuielf c:/boot/grub/kernel.bin
	@rm -f ./menu.lst ~/.mtoolsrc bmap
	@echo
	@echo "*********************************************************************************"
	@echo "            Bootable HD image 'boot.img' was successfilly created."
	@echo "       Make sure the following lines are present in your 'bochsrc' file:"
	@echo " ata0-master: type=disk, path=boot.img, mode=flat, cylinders=44, heads=16, spt=63"
	@echo " boot: disk"
	@echo "*********************************************************************************"
endif

$(VERFILE):	
	$(Q)$(ECHO) "#ifndef __VERSION_H__" > $(VERFILE)
	$(Q)$(ECHO) "#define __VERSION_H__" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_VERSION $(KERNEL_MAJOR_VER)" >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_SUBVERSION $(KERNEL_MIDDLE_VER)" >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_RELEASE $(KERNEL_MINOR_VER)" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#define KERNEL_RELEASE_NAME \"$(KERNEL_NAME)\"" >> $(VERFILE)
	$(Q)$(ECHO) >> $(VERFILE)
	$(Q)$(ECHO) "#endif /* __VERSION_H__ */" >> $(VERFILE)

config: host
	$(Q)$(MKDIR) -p $(BUILD_ROOT)/include/config
	$(Q)$(MAKE) -C kbuild conf BUILD_ROOT=$(BUILD_ROOT)
	$(Q)$(BUILD_ROOT)/kbuild/conf $(BUILD_ROOT)/kernel/arch/Kconfig

menuconfig: host
	$(Q)$(MKDIR) -p $(BUILD_ROOT)/include/config
	$(Q)$(MAKE) mconf -C kbuild BUILD_ROOT=$(BUILD_ROOT)
	$(Q)$(BUILD_ROOT)/kbuild/mconf $(BUILD_ROOT)/kernel/arch/Kconfig
	$(Q)$(BUILD_ROOT)/kbuild/conf -s $(BUILD_ROOT)/kernel/arch/Kconfig

help:
	$(Q)$(ECHO) "USAGE: make [action] [OPTIONS] [VARIABLES]"
	$(Q)$(ECHO) "   OPTIONS:"
	$(Q)$(ECHO) "      NOCOLOR=[y/n]       Enable/disable color (default: enabled)"
	$(Q)$(ECHO) "      VERBOSE=[y/n]       Enable/disable verbose mode (default: disabled)"
	$(Q)$(ECHO) "      target=<dir>        Specify building directory"
	$(Q)$(ECHO) "      TOOLCHAIN=<prefix>  Specify toolchain prefix"
	$(Q)$(ECHO) "      NOBUILDIMG=[y/n]    Toggle image building"
	$(Q)$(ECHO) "    Customizible variables:"
	$(Q)$(ECHO) "      CFLAGS, LDFLAGS, INCLUDE, HOSTCC, HOSTCFLAGS, HOSTLDFLAGS"
	$(Q)$(ECHO) "    Available actions:"
	$(Q)$(ECHO) "      make [config|menuconfig] arch=<your_arch> - configure the kernel"
	$(Q)$(ECHO) "      make all - build all"
	$(Q)$(ECHO) "      make host - build host utilites"
	$(Q)$(ECHO) "      make vmuielf - build kernel image"
	$(Q)$(ECHO) "      make clean - clean directories from object files"
	$(Q)$(ECHO) "      make cleanconf - remove config"
	$(Q)$(ECHO) "      make distclean - combines two actions above"

help_config:
	$(Q)$(ECHO) "Before building kernel you should configure it"
	$(Q)$(ECHO) "Run make [config or menuconfig] arch=<your_arch>"
	$(Q)$(ECHO) "Supported architectures:"
	$(Q)$(ECHO)
	$(Q)exit 2

