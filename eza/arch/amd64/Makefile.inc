ATSIGN ?= @
SYMTAB_SECTION := ".section symtab.data, \"a\", $(ATSIGN)progbits;"
CFLAGS += -m64 -mcmodel=kernel -mno-red-zone -fno-unwind-tables

define pre_linking_action
	$(Q)$(ECHO) $(SYMTAB_SECTION) | $(AS) $(ASFLAGS) -o $(ODIR)/emap.o
endef

define post_linking_action
	$(Q)$(ECHO) $(SYMTAB_SECTION)" .incbin \"$@\"" | $(AS) $(ASFLAGS) -o $(ODIR)/emapo.o
endef

define create_rmap
	$(call echo-action,"AS","$^")
	$(Q)$(ECHO) $(SYMTAB_SECTION)" .incbin \"$(ODIR)/rmap.o\"" | $(AS) $(ASFLAGS) -o $(ODIR)/rmap.o	
endef
