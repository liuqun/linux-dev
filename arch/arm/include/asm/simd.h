/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <linux/simd.h>
#ifndef _ASM_SIMD_H
#define _ASM_SIMD_H

#ifdef CONFIG_KERNEL_MODE_NEON
#include <asm/neon.h>

static __must_check inline bool may_use_simd(void)
{
	return !in_nmi() && !in_irq() && !in_serving_softirq();
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

#else

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
#endif

#endif /* _ASM_SIMD_H */
