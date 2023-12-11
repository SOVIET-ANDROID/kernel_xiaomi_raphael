/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_STACK_POINTER_H
#define __ASM_STACK_POINTER_H

/*
 * how to get the current stack pointer from C
 */
#define current_stack_pointer \
({									\
	register unsigned long __current_stack_pointer asm ("sp");	\
									\
	__current_stack_pointer;					\
})

#endif /* __ASM_STACK_POINTER_H */
