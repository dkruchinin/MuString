#!/bin/bash

APP=`basename $0`
PT=mbr

usage()
{
    echo "Usage: ${APP} <image_file> <kernel_elf>"
    exit $*
}

TMP=`getopt -o hp:k:i: --long help,partition:,kernel:,image: -n ${APP} -- "$@"`
[ $? != 0 ] && usage 1
eval set -- "$TMP"
while true ; do
	case "$1" in
		-h|--help) usage 0 ; shift ;;
		-p|--partition)
			case "$2" in
				"gpt"|"mbr") PT=$2 ; shift 2 ;;
				*)  echo "Unsupported partition type '$2'" >&2 ; exit 1 ;;
			esac ;;
		-k|--kernel) KERNEL_ELF=$2 ; shift 2 ;;
		-i|--image) IMAGE_FILE=$2 ; shift 2 ;;
		--) shift ; break ;;
		*) echo "Internal error!" ; exit 1 ;;
	esac
done

[ -z ${IMAGE_FILE} ] && 
{
	[ $# -gt 0 ] || usage 1
	IMAGE_FILE=$1
	shift
}

[ -z ${KERNEL_ELF} ] &&
{
	[ $# -gt 0 ] || usage 1
	KERNEL_ELF=$1
	shift
}

dd if=/dev/zero of=${IMAGE_FILE} bs=8192 count=65536
case "${PT}" in
	"mbr")
		sfdisk -D ${IMAGE_FILE} <<-EOF
			,,L,*
		EOF
		MODS="part_msdos" ;
		;;
	"gpt")
		sgdisk -a 1 -n 0:0:-4096K -a 1 -n 0:0:0 -t 2:ef02 ${IMAGE_FILE} ;
#		sgdisk -h 1 ${IMAGE_FILE} ;
#		sfdisk -A2 ${IMAGE_FILE} ;
		MODS="part_gpt" ;
		;;
	*)  echo "Unexpected partition type '${PT}'" >&2 ; exit 1 ;;
esac

BLOCK_DEVICE=`losetup --partscan --find --show ${IMAGE_FILE}`
mke2fs -b 1024 -t ext2 ${BLOCK_DEVICE}p1
MOUNT_POINT=`mktemp --directory`
mount -t ext2 ${BLOCK_DEVICE}p1 ${MOUNT_POINT}
grub2-install --boot-directory=${MOUNT_POINT}/boot --modules="${MODS} ext2" ${BLOCK_DEVICE}
#grub2-install --target=i386-multiboot --boot-directory=${MOUNT_POINT}/boot --force --modules="${MODS} ext2 multiboot multiboot2" --debug ${BLOCK_DEVICE}
#grub2-mkimage -O i386-multiboot -o ${MOUNT_POINT}/boot/grub2/i386-multiboot/core.img -v
#grub2-bios-setup -s -m /dev/null -d /usr/lib/grub -b i386-pc/boot.img -c ../../..${MOUNT_POINT}/boot/grub2/i386-multiboot/core.img -v ${BLOCK_DEVICE}
install ${KERNEL_ELF} ${MOUNT_POINT}/boot/
NAME=`basename ${KERNEL_ELF}`
cat > ${MOUNT_POINT}/boot/grub2/grub.cfg <<EOF
	set default=0
	set timeout=30
	insmod gfxterm
	insmod vbe
	if terminal_output gfxterm ; then true ; else terminal gfxterm; fi
	set color_normal=black/white
	set menu_color_normal=green/light-blue
	set menu_color_highlight=yellow/blue
	menuentry "MString kernel" {
		insmod multiboot
		insmod multiboot2
		search -n -f /boot/grub2/grub.cfg -s
		multiboot /boot/${NAME}
EOF
for MOD do
{
	NAME=`basename ${MOD}`
	install ${MOD} ${MOUNT_POINT}/boot/ && echo -e "\t\tmodule /boot/${NAME}" >> ${MOUNT_POINT}/boot/grub2/grub.cfg
} ; done
echo -e "\t}\n" >> ${MOUNT_POINT}/boot/grub2/grub.cfg

umount ${MOUNT_POINT}
losetup --detach ${BLOCK_DEVICE}
rmdir ${MOUNT_POINT}
exit 0
