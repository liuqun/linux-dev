/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2017 Linaro Ltd. <ard.biesheuvel@linaro.org>
 * Copyright (C) 2015-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <linux/simd.h>
#ifndef __ASM_SIMD_H
#define __ASM_SIMD_H

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/types.h>

#ifdef CONFIG_KERNEL_MODE_NEON
#include <asm/neon.h>
#include <asm/simd.h>

DECLARE_PER_CPU(bool, kernel_neon_busy);

/*
 * may_use_simd - whether it is allowable at this time to issue SIMD
 *                instructions or access the SIMD register file
 *
 * Callers must not assume that the result remains true beyond the next
 * preempt_enable() or return from softirq context.
 */
static __must_check inline bool may_use_simd(void)
{
	/*
	 * kernel_neon_busy is only set while preemption is disabled,
	 * and is clear whenever preemption is enabled. Since
	 * this_cpu_read() is atomic w.r.t. preemption, kernel_neon_busy
	 * cannot change under our feet -- if it's set we cannot be
	 * migrated, and if it's clear we cannot be migrated to a CPU
	 * where it is set.
	 */
	return !in_irq() && !irqs_disabled() && !in_nmi() &&
		!this_cpu_read(kernel_neon_busy);
}

static inline void simd_get(simd_context_t *ctx)
{
	*ctx = may_use_simd() ? HAVE_FULL_SIMD : HAVE_NO_SIMD;
}

static inline void simd_put(simd_context_t *ctx)
{
	if (*ctx & HAVE_SIMD_IN_USE)
		kernel_neon_end();
	*ctx = HAVE_NO_SIMD;
}

static __must_check inline bool simd_use(simd_context_t *ctx)
{
	if (!(*ctx & HAVE_FULL_SIMD))
		return false;
	if (*ctx & HAVE_SIMD_IN_USE)
		return true;
	kernel_neon_begin();
	*ctx |= HAVE_SIMD_IN_USE;
	return true;
}

#else /* ! CONFIG_KERNEL_MODE_NEON */

static __must_check inline bool may_use_simd(void)
{
	return false;
}

static inline void simd_get(simd_context_t *ctx)
{
	*ctx = HAVE_NO_SIMD;
}

static inline void simd_put(simd_context_t *ctx)
{
}

static __must_check inline bool simd_use(simd_context_t *ctx)
{
	return false;
}

#endif /* ! CONFIG_KERNEL_MODE_NEON */

#endif
