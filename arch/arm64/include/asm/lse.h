/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LSE_H
#define __ASM_LSE_H

#if defined(CONFIG_AS_LSE) && defined(CONFIG_ARM64_LSE_ATOMICS)

#define __LSE_PREAMBLE	".arch armv8-a+lse\n"

#ifdef __ASSEMBLER__

#else	/* __ASSEMBLER__ */

__asm__(".arch_extension	lse");

#define ARM64_LSE_ATOMIC_INSN(lse)					\
	__LSE_PREAMBLE lse

#endif	/* __ASSEMBLER__ */
#else	/* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */

#ifndef __ASSEMBLER__

#define __LL_SC_INLINE		static inline
#define __LL_SC_PREFIX(x)	x
#define __LL_SC_EXPORT(x)

#endif	/* __ASSEMBLER__ */
#endif	/* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */
#endif	/* __ASM_LSE_H */
