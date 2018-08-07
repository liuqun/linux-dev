ccflags-y := -O2
ccflags-y += -D'pr_fmt(fmt)="zinc: " fmt'
ccflags-$(CONFIG_ZINC_DEBUG) += -DDEBUG

zinc_chacha20-y := chacha20/chacha20.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_X86_64) += chacha20/chacha20-x86_64.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_ARM) += chacha20/chacha20-arm.o chacha20/chacha20-unrolled-arm.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_ARM64) += chacha20/chacha20-arm64.o
perlasm-targets += chacha20/chacha20-x86_64.S chacha20/chacha20-arm.S chacha20/chacha20-arm64.S
obj-$(CONFIG_ZINC_CHACHA20) += zinc_chacha20.o

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $< > $@
$(addprefix $(obj)/,$(perlasm-targets)): $(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perlasm)
targets += $(perlasm-targets)
