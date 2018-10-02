ccflags-y := -O2
ccflags-y += -D'pr_fmt(fmt)="zinc: " fmt'
ccflags-$(CONFIG_ZINC_DEBUG) += -DDEBUG

zinc_chacha20-y := chacha20/chacha20.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_X86_64) += chacha20/chacha20-x86_64.o
perlasm-targets += chacha20/chacha20-x86_64.S
obj-$(CONFIG_ZINC_CHACHA20) += zinc_chacha20.o

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $< > $@
$(addprefix $(obj)/,$(perlasm-targets)): $(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perlasm)
targets += $(perlasm-targets)
