#$(target)/kernel.ld: $(BUILD_ROOT)/eza/$(ARCH)/kernel.ld.S
#	$(Q)$(ECHO) "[PS] $^"
#	$(Q)$(CC) $(CFLAGS) $(EXTRFL) -D__ASM__ -E -x c $< | $(GREP) -v "^\#" > $@

include config
include include/Makefile.inc
include $(BUILD_ROOT)/$(target)/Makefile

OBJDIR := $(target)/objects
objs := $(addprefix $(OBJDIR)/, $(filter %.o,$(obj-y)))
dirs := $(filter-out %.o, $(obj-y))

all: mkobjdir $(objs) $(addprefix dir_$(target)/, $(dirs))

mkobjdir:
	$(MKDIR) -p $(OBJDIR)

$(OBJDIR)/%.o: $(target)/%.S
	$(call PRINT_LABEL,"AS")
	$(Q)$(ECHO) "$(notdir $@)"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -n -D__ASM__ -c -o $@ $<

$(OBJDIR)/%.o: $(target)/%.c
	$(call PRINT_LABEL,"CC")
	$(Q)$(ECHO) "$(notdir $@)"
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -c -o $@ $^

dir_$(target)/%:
	$(call PRINT_SUBHDR,"+","$(subst dir_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst dir_,,$@)

clean_$(target)/%:
	$(call PRINT_SUBHDR,"-","$(subst clean_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst clean_,,$@) clean

clean: $(addprefix clean_$(target)/, $(dirs))
	$(Q)$(shell [ -d $(OBJDIR) ] && $(RM) -rf $(OBJDIR))
