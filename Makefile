# 
# (c) Copyright MString core development team <http://mstring.berlios.de>
# (c) Copyright Tirra <tirra.newly@gmail.com> 
#
# Makefile: general makefile for MuiString
#

-include config
-include Makefile.inc

# libs
-include eza/generic_api/Makefile.inc
-include mm/Makefile.inc
-include mlibc/Makefile.inc

# arch specific
-include eza/$(ARCH)/Makefile.inc

CFLAGS+=-Iinclude/

ifeq ($(PARANOIC),y)
     CFLAGS += -Werror
endif

MUIOBJS=$(LIBEZA_OBJS) $(LIBARCHEZA_OBJS) $(LIBMM_OBJS) \
        $(LIBEZA_CORE_OBJS) $(MLIBC_OBJS)

all: build bootimage

build: make-symlinks vmuielf

make-symlinks:
	$(Q)$(LN) -snf $(ARCH) include/eza/arch

%.o: %.S
	$(Q)$(ECHO) "[AS] $^"
	$(Q)$(CC) $(CFLAGS) $(EXTRFL) -n -D__ASM__ -c -o $@ $<

%.o: %.c
	$(Q)$(ECHO) "[CC] $^"
	$(Q)$(CC) $(CFLAGS) $(EXTRFL) -c -o $@ $^

kernel.ld: eza/$(ARCH)/kernel.ld.S
	$(Q)$(ECHO) "[PS] $^"
	$(Q)$(CC) $(CFLAGS) $(EXTRFL) -D__ASM__ -E -x c $< | $(GREP) -v "^\#" > $@

vmuielf: muielf
	$(Q)$(ECHO) "[OBJCOPY] $@"
	$(Q)$(OBJCOPY) -O binary $< $@

muielf: rmap.o
	$(Q)$(ECHO) "[LD] $^"
	$(Q)$(LD) -T kernel.ld $(LDFLAGS) $(MUIOBJS) rmap.o -o $@ -Map muielf.map

rmap.bin: $(MUIOBJS) kernel.ld
	$(Q)$(ECHO) $(SYMTAB_SECTION) | $(AS) $(ASFLAGS) -o emap.o
	$(Q)$(LD) -T kernel.ld $(LDFLAGS) -q $(MUIOBJS) emap.o -o $@ -Map mui.pre.map
	$(Q)$(OBJDUMP) -t $(MUIOBJS) > mui.objdump
	$(Q)$(GMAP) mui.pre.map mui.objdump rmap.bin 
	$(Q)$(ECHO) "[MK] Regeneration ..."
	$(Q)$(ECHO) $(SYMTAB_SECTION)" .incbin \"$@\"" | $(AS) $(ASFLAGS) -o emapo.o
	$(Q)$(LD) -T kernel.ld $(LDFLAGS) $(MUIOBJS) emapo.o -o $@ -Map mui.pre.map
	$(Q)$(OBJDUMP) -t $(MUIOBJS) > mui.objdump
	$(Q)$(GMAP) mui.pre.map mui.objdump rmap.bin


rmap.o: rmap.bin
	$(Q)$(ECHO) "[AS] $^"
	$(Q)$(ECHO) $(SYMTAB_SECTION)" .incbin \"$<\"" | $(AS) $(ASFLAGS) -o $@

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

clean:
	rm -f $(MUIOBJS)
	rm -f muielf muielf.map kernel.ld *.map *.o *.raw *.objdump *.bin vmuielf boot.img
