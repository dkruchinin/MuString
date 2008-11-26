define PRINT_HEADER
	$(if $(NOCOLOR), 
	$(Q)$(ECHO) -n "[*] ",
	$(Q)$(ECHO) -n -e "[\e[1;31m*\e[0m] ")
    $(Q)$(ECHO) "$(1)"
endef

define PRINT_SUBHDR
	$(if $(NOCOLOR), 
	$(Q)$(ECHO) -n "  [$1] ",
	$(Q)$(ECHO) -n -e "  [\e[1;31m$1\e[0m] ")
    $(Q)$(ECHO) "$(2)"
endef

define PRINT_LABEL
	$(if $(NOCOLOR), 
	$(Q)$(ECHO) -n "   [$1] ",
	$(Q)$(ECHO) -n -e "   [\e[1;32m$1\e[0m] ")
endef

define collect_objects
	$(shell find . -name "*.o")
endef