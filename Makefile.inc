
-include config

CC = /usr/bin/gcc 
MAKE = /usr/bin/make -s
LD = /usr/bin/ld
OBJDUMP = /usr/bin/objdump
OBJCOPY = /usr/bin/objcopy
AS = /usr/bin/as
ASFLAGS = 
CFLAGS = -Wall -nostdlib -nostdinc -fno-builtin -fomit-frame-pointer
LDFLAGS = -M
Q = @
GREP = /bin/grep
ECHO = /bin/echo
LN = /bin/ln
GMAP = eza/gmap.py


ATSIGN ?= @

SYMTAB_SECTION=".section symtab.data, \"a\", $(ATSIGN)progbits;"
