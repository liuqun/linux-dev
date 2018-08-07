ccflags-y := -O2
ccflags-y += -D'pr_fmt(fmt)="zinc: " fmt'
ccflags-$(CONFIG_ZINC_DEBUG) += -DDEBUG

zinc_chacha20-y := chacha20/chacha20.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_X86_64) += chacha20/chacha20-x86_64.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_ARM) += chacha20/chacha20-arm.o chacha20/chacha20-unrolled-arm.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_ARM64) += chacha20/chacha20-arm64.o
zinc_chacha20-$(CONFIG_ZINC_ARCH_MIPS) += chacha20/chacha20-mips.o
AFLAGS_chacha20-mips.o += -O2 # This is required to fill the branch delay slots
perlasm-targets += chacha20/chacha20-x86_64.S chacha20/chacha20-arm.S chacha20/chacha20-arm64.S
obj-$(CONFIG_ZINC_CHACHA20) += zinc_chacha20.o

zinc_poly1305-y := poly1305/poly1305.o
zinc_poly1305-$(CONFIG_ZINC_ARCH_X86_64) += poly1305/poly1305-x86_64.o
zinc_poly1305-$(CONFIG_ZINC_ARCH_ARM) += poly1305/poly1305-arm.o
zinc_poly1305-$(CONFIG_ZINC_ARCH_ARM64) += poly1305/poly1305-arm64.o
zinc_poly1305-$(CONFIG_ZINC_ARCH_MIPS) += poly1305/poly1305-mips.o
AFLAGS_poly1305-mips.o += -O2 # This is required to fill the branch delay slots
zinc_poly1305-$(CONFIG_ZINC_ARCH_MIPS64) += poly1305/poly1305-mips64.o
perlasm-targets += poly1305/poly1305-x86_64.S poly1305/poly1305-arm.S poly1305/poly1305-arm64.S poly1305/poly1305-mips64.S
obj-$(CONFIG_ZINC_POLY1305) += zinc_poly1305.o

zinc_chacha20poly1305-y := chacha20poly1305.o
obj-$(CONFIG_ZINC_CHACHA20POLY1305) += zinc_chacha20poly1305.o

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $< > $@
$(addprefix $(obj)/,$(perlasm-targets)): $(obj)/%.S: $(src)/%.pl FORCE
	$(call if_changed,perlasm)
targets += $(perlasm-targets)
