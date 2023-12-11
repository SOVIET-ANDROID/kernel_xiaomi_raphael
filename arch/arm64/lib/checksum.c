/*
 * Authors: huhai <huhai@kylinos.cn>
 * Copyright (C) 2019,Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This file contains network checksum routines that are better done
 * in an architecture-specific manner due to speed.
 *
 * Acknowledgements:
 * This file is based on arch/x86/lib/csum-partial_64.c and
 * arch/alpha/lib/checksum.c, which was written by Thomas Gleixner
 * and Rick Gorton respectively.
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <asm/checksum.h>

static inline unsigned short from64to16(unsigned long x)
{
	/*
	 * Using extract instructions is a bit more efficient
	 * than the original shift/bitmask version.
	 */
	union {
		unsigned long   ul;
		unsigned int    ui[2];
		unsigned short  us[4];
	} in_v, tmp_v, out_v;

	in_v.ul = x;
	tmp_v.ul = (unsigned long) in_v.ui[0] + (unsigned long) in_v.ui[1];

	/*
	 * Since the bits of tmp_v.sh[3] are going to always be zero,
	 * we don't have to bother to add that in.
	 */
	out_v.ul = (unsigned long) tmp_v.us[0] + (unsigned long) tmp_v.us[1]
			+ (unsigned long) tmp_v.us[2];

	/* Similarly, out_v.us[2] is always zero for the final add. */
	return out_v.us[0] + out_v.us[1];
}

/*
 * Do a 64-bit checksum on an arbitrary memory area.
 * Returns a 16bit checksum.
 */
unsigned int _do_csum(const unsigned char *buff, unsigned len)
{
	unsigned odd, count;
	unsigned long result = 0;

	if (unlikely(len == 0))
		return result;
	odd = 1 & (unsigned long) buff;
	if (odd) {
		result = *buff << 8;
		len--;
		buff++;
	}
	count = len >> 1;		/* nr of 16-bit words.. */
	if (count) {
		if (2 & (unsigned long) buff) {
			result += *(unsigned short *)buff;
			count--;
			len -= 2;
			buff += 2;
		}
		count >>= 1;		/* nr of 32-bit words.. */
		if (count) {
			unsigned long zero = 0;
			unsigned count64;

			if (4 & (unsigned long) buff) {
				result += *(unsigned int *) buff;
				count--;
				len -= 4;
				buff += 4;
			}
			count >>= 1;	/* nr of 64-bit words.. */

			/* main loop using 64byte blocks */
			count64 = count >> 3;
			while (count64) {
				__asm__ __volatile__(
					"ldr x3, [%x1, #0]\n"
					"adds %x0, %x0, x3\n"
					"ldr x3, [%x1, #8]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #16]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #24]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #32]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #40]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #48]\n"
					"adcs %x0, %x0, x3\n"
					"ldr x3, [%x1, #56]\n"
					"adcs %x0, %x0, x3\n"
					"adcs %x0, %x0, %x2\n"
					: "=r" (result)
					: "r" (buff), "r" (zero), "0" (result)
					: "cc", "memory", "x3");

				buff += 64;
				count64--;
			}

			/* last up to 7 8byte blocks */
			count %= 8;

			while (count) {
				__asm__ __volatile__(
					"adds %x0, %x0, %x1\n"
					"adcs %x0, %x0, %x2\n"
					: "=r" (result)
					: "r" (*(unsigned long *)buff), "r" (zero), "0" (result)
					: "cc", "memory");
				--count;
				buff += 8;
			}
			result = (result & 0xffffffff) + (result >> 32);

			if (len & 4) {
				result += *(unsigned int *) buff;
				buff += 4;
			}
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += *buff;

	result = from64to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);

	return result;
}