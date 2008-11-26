CC := $(TOOLCHAIN)gcc
AR := $(TOOLCHAIN)ar
AS := $(TOOLCHAIN)as
LD := $(TOOLCHAIN)ld
OBJDUMP := $(TOOLCHAIN)objdump
OBJCOPY := $(TOOLCHAIN)objcopy

MAKE := make -s
GREP := grep
GMAP := eza/gmap.py
LN := ln
RM := rm
MKDIR := mkdir
Q := @
ECHO := echo

ifeq ($(VERBOSE), y)
	Q := 
endif
ifneq ($(NOCOLOR), y)
	$NOCOLOR :=
endif

CFLAGS += -Wall -nostdlib -nostdinc -fno-builtin -fomit-frame-pointer -DCONFIG_SMP -g
LDFLAGS += -M
INCLUDE += -Iinclude

ifndef $(BUILD_ROOT)
	BUILD_ROOT := $(CURDIR)
endif

ODIR := vmjari_objs
OBJECTS :=

export CC LD AR OBJDUMP OBJCOPY GMAP GREP CPP AS ECHO
export GREP MAKE LN RM GMAP Q MKDIR
export CFLAGS LDFLAGS INCLUDE
export BUILD_ROOT ARCH NOCOLOR OBJECTS

GENERICS = mm mlibc ipc server eza

include config
include include/Makefile.inc
include eza/$(ARCH)/Makefile.inc

define create_symlinks
	$(Q)$(shell [ -e $(BUILD_ROOT)/include/eza/arch ] && $(RM) -f $(BUILD_ROOT)/include/eza/arch)
	$(Q)$(LN) -snf $(ARCH) include/eza/arch
endef

.PHONY: all vmuielf rmap.bin collect_objects
all: vmuielf bootimage

vmuielf: prepare $(addprefix generic_, $(GENERICS)) muielf
	$(call PRINT_LABEL,"OBJCOPY")
	$(Q)$(ECHO) "$@"
	$(Q)$(OBJCOPY) -O binary muielf $@

prepare:
	$(call create_symlinks)
	$(Q)$(MKDIR) -p $(ODIR)

muielf: $(addprefix $(ODIR)/, kernel.ld rmap.o)	
	$(call PRINT_LABEL,"LD")
	$(Q)$(ECHO) "$^"
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) $(OBJECTS) $(ODIR)/rmap.o -o $@ -Map $(ODIR)/muielf.map

generic_%:
	$(call PRINT_HEADER,"$(subst generic_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst generic_,,$@)

clean_%:
	$(call PRINT_HEADER,"Cleaning $(subst clean_,,$@):")
	$(Q)$(MAKE) -f rules.mak target=$(subst clean_,,$@) clean

clean:
	$(Q)$(RM) -rf $(ODIR)
	$(Q)$(RM) -f muielf vmuielf
	$(Q)$(MAKE) -C. $(addprefix clean_, $(GENERICS))

$(ODIR)/rmap.o: $(ODIR)/rmap.bin
	$(call create_rmap)

$(ODIR)/kernel.ld: $(BUILD_ROOT)/eza/$(ARCH)/kernel.ld.S
	$(call PRINT_LABEL,"CPP")
	$(Q)$(ECHO) "$<"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -D__ASM__ -E -x c $< | $(GREP) -v "^\#" > $@

collect_objects:
	$(eval OBJECTS := $(call collect_objects))

$(ODIR)/rmap.bin: collect_objects $(ODIR)/kernel.ld
	$(call pre_linking_action)	
	$(call PRINT_LABEL,"LD")
	$(Q)$(ECHO) "Linking all together..."
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) -q $(OBJECTS) -o $@ -Map $(ODIR)/mui.pre.map	
	$(Q)$(OBJDUMP) -t $(OBJECTS) > $(ODIR)/mui.objdump
	$(Q)$(GMAP) $(addprefix $(ODIR)/, mui.pre.map mui.objdump rmap.bin)
	$(call PRINT_LABEL,"MK")
	$(Q)$(ECHO) "Regeneration ..."
	$(call post_linking_action)
	$(Q)$(LD) -T $(ODIR)/kernel.ld $(LDFLAGS) $(OBJECTS) $(ODIR)/emapo.o -o $@ -Map $(ODIR)/mui.pre.map
	$(Q)$(OBJDUMP) -t $(OBJECTS) > $(ODIR)/mui.objdump	
	$(Q)$(GMAP) $(addprefix $(ODIR)/, mui.pre.map mui.objdump rmap.bin)

bootimage: vmuielf
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

