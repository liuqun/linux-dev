ccflags-y := -O2
ccflags-y += -D'pr_fmt(fmt)="zinc: " fmt'
ccflags-$(CONFIG_ZINC_DEBUG) += -DDEBUG

zinc_chacha20-y := chacha20/chacha20.o
obj-$(CONFIG_ZINC_CHACHA20) += zinc_chacha20.o
