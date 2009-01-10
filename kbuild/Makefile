include $(BUILD_ROOT)/include/Makefile.inc
#include ../include/Makefile.inc
# lxdialig related variables
lxdialog_root := $(BUILD_ROOT)/kbuild/lxdialog
check-lxdialog := $(lxdialog_root)/check-lxdialog.sh
HOSTCFLAGS = $(shell bash $(check-lxdialog) -ccflags)
HOSTLDFLAGS += "$(shell bash $(check-lxdialog) -ldflags $(HOSTCC))"
lxdialog-objs := $(addprefix lxdialog/, checklist.o util.o inputbox.o textbox.o yesno.o menubox.o)

# menuconfig related things
conf-objs := conf.o zconf.tab.o
mconf-objs := $(lxdialog-objs) mconf.o zconf.tab.o

all: conf mconf

conf: $(conf-objs)
	$(call echo-action,"LD","$@")
	$(Q)$(HOSTCC) $(HOSTCFLAGS) $(HOSTLDFLAGS) $^ -o $@

mconf: $(mconf-objs)
	$(call echo-action,"LD","$@")	
	$(HOSTCC) $(HOSTCFLAGS) $(HOSTLDFLAGS) $^ -o $@

zconf.tab.c: lex.zconf.c zconf.hash.c

clean:
	$(Q)$(RM) -f $(mconf-objs) lex.zconf.c zconf.hash.c zconf.tab.c *.o conf mconf

###
# The following requires flex/bison/gperf
# By default we use the _shipped versions, uncomment the following line if
# you are modifying the flex/bison src.
LKC_GENPARSER := 1

ifdef LKC_GENPARSER
zconf.tab.c: zconf.y
lex.zconf.c: zconf.l
zconf.hash.c: zconf.gperf

%.tab.c: %.y
	$(call echo-action,"BISON","$@")
	$(Q)bison -l -b $* -p $(notdir $*) $<
	$(Q)$(CP) $@ $@_shipped

lex.%.c: %.l
	$(call echo-action,"LEX","$@")
	$(Q)flex -L -P$(notdir $*) -o$@ $<
	$(Q)$(CP) $@ $@_shipped

%.hash.c: %.gperf
	$(call echo-action,"GPERF","$@")
	$(Q)gperf < $< > $@
	$(Q)$(CP) $@ $@_shipped
endif

%.o: %.c
	$(call echo-action,"HOSTCC","$@")
	$(Q)$(HOSTCC) $(HOSTLDFLAGS) $(HOSTCFLAGS) -c -o $@ $^

.PHONY: menuconfig
