define colored
$(if $(NOCOLOR),$(2),$(1)$(2)\e[0m)
endef

define echo-header
	$(Q)$(ECHO) -e "[$(call colored,\e[1;31m,*)] $(1)"
endef

define echo-action
	$(Q)$(ECHO) -e "   [$(call colored,\e[1;32m,$(1))] $(2)"
endef

define echo-section
	$(Q)$(ECHO) -e "  [$(call colored,\e[1;31m,$(1))] $(2)"
endef

define collect_objects
	$(shell find $(GENERICS) -name "*.o")
endef

define create_symlinks
	$(Q)$(shell [ -e $(BUILD_ROOT)/include/eza/arch ] && $(RM) -f $(BUILD_ROOT)/include/eza/arch)
	$(Q)$(LN) -snf $(ARCH) include/eza/arch
endef

define show_archs
	$(foreach a,$(subst eza/arch/,,$(shell find eza/arch/ -maxdepth 1 -type d -print)),$(Q)$(ECHO) " * $(a)")
endef