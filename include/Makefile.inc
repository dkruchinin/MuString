define colored
$(if $(NOCOLOR),$(1),$(1)$(2)\e[0m)
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
	$(shell [ -e $(BUILD_ROOT)/include/eza/arch ] && $(RM) -f $(BUILD_ROOT)/include/eza/arch)
	$(LN) -snf $(ARCH) include/eza/arch
endef
