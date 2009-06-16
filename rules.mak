-include .config
include include/Makefile.inc
-include $(BUILD_ROOT)/$(target)/Makefile

OBJDIR := $(target)/binaries
objs := $(addprefix $(OBJDIR)/, $(filter %.o,$(obj-y)))
dirs := $(filter-out %.o, $(obj-y))

all: mkobjdir $(objs) $(addprefix dir_$(target)/, $(dirs))

mkobjdir:
	$(MKDIR) -p $(OBJDIR)

$(OBJDIR)/%.o: $(target)/%.S
	$(call echo-action,"AS","$(notdir $@)")
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -n -D__ASM__ -c $< -o $@

$(OBJDIR)/%.o: $(target)/%.c
	$(call echo-action,"CC","$(notdir $@)")
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -c $^ -o $@

dir_$(target)/%:
	$(call echo-section,"+","$(subst dir_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst dir_,,$@)

clean_$(target)/%:
	$(call echo-section,"-","$(subst clean_,,$@)")
	$(Q)$(MAKE) -f rules.mak target=$(subst clean_,,$@) clean

clean: $(addprefix clean_$(target)/, $(dirs))
	$(Q)$(shell [ -d $(OBJDIR) ] && $(RM) -rf $(OBJDIR))
