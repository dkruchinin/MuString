define echo-header
	$(Q)$(ECHO) "*** $(1) ***"
endef

define echo-action
	$(Q)$(ECHO) " [$(1)] $(2)"
endef

define echo-section
	$(Q)$(ECHO) "[$(1)] $(2)"
endef

define collect_objects
	$(shell find $(GENERICS) -name "*.o")
endef

define create_symlinks
	$(Q)$(shell [ -e $(BUILD_ROOT)/include/arch ] && $(RM) -f $(BUILD_ROOT)/include/arch)
	$(Q)$(LN) -snf $(BUILD_ROOT)/kernel/arch/$(ARCH)/include $(BUILD_ROOT)/include/arch
endef

define show_archs
	$(foreach a,$(subst eza/arch/,,$(shell find eza/arch/ -maxdepth 1 -type d -print)),$(Q)$(ECHO) " * $(a)")
endef

define posconfig_actions
	$(Q)$(ECHO) "ARCH=$(1)" >> .config
	$(Q)$(ECHO) "#ifndef __VERSION_H__" > $(2)
	$(Q)$(ECHO) "#define __VERSION_H__" >> $(2)
	$(Q)$(ECHO) >> $(2)
	$(Q)$(ECHO) "#define KERNEL_VERSION $(KERNEL_MAJOR_VER)" >> $(2)
	$(Q)$(ECHO) "#define KERNEL_SUBVERSION $(KERNEL_MIDDLE_VER)" >> $(2)
	$(Q)$(ECHO) "#define KERNEL_RELEASE $(KERNEL_MINOR_VER)" >> $(2)
	$(Q)$(ECHO) >> $(2)
	$(Q)$(ECHO) "#define KERNEL_RELEASE_NAME \"$(KERNEL_NAME)\"" >> $(2)
	$(Q)$(ECHO) >> $(2)
	$(Q)$(ECHO) "#endif /* __VERSION_H__ */" >> $(2)
endef