CFLAGS += -m64 -mcmodel=kernel -mno-red-zone -fno-unwind-tables
CFLAGS += -mno-sse -mno-mmx -mno-sse2 -mno-3dnow

$(ODIR)/kernel.ld: $(ARCH_DIR)/src/kernel.ld.S
	$(call echo-action "CPP", "$<")
	$(Q)$(CC) $(CFLAGS) $(INCLUDE) -D__ASM__ -E -x c $< |  $(GREP) -v "^\#" > $@
