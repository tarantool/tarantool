/*
 * Copyright (C) 2010 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <errno.h>

#include "cpu_feature.h"

enum { eAX=0, eBX, eCX, eDX };

static const struct cpuid_feature {
	unsigned int 	ri;
	u_int32_t 		mask;
} cpu_ftr[] = {
	{eDX, (1 << 28)},	/* HT 		*/
	{eCX, (1 << 19)},	/* SSE 4.1 	*/
	{eCX, (1 << 20)},	/* SSE 4.2 	*/
	{eCX, (1 << 31)}	/* HYPERV	*/
};
static const size_t LEN_cpu_ftr = sizeof(cpu_ftr) / sizeof (cpu_ftr[0]);

#define SCALE_F		sizeof(unsigned long)

#if defined (__x86_64__)
	#define REX_PRE "0x48, "
#elif defined (__i386__)
	#define REX_PRE
#else
	#error "Only x86 and x86_64 architectures supported"
#endif


/* hw-calculate CRC32 per byte (for the unaligned portion of data buffer)
 */
static u_int32_t
crc32c_hw_byte(u_int32_t crc, unsigned char const *data, size_t length)
{
	while (length--) {
		__asm__ __volatile__(
			".byte 0xf2, 0xf, 0x38, 0xf0, 0xf1"
			:"=S"(crc)
			:"0"(crc), "c"(*data)
		);
		data++;
	}

	return crc;
}


/* hw-calculate CRC32 for the given data buffer
 */
u_int32_t
crc32c_hw(u_int32_t crc, unsigned char const *p, size_t len)
{
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
	unsigned long *ptmp = (unsigned long *)p;

	while (iquotient--) {
		__asm__ __volatile__(
			".byte 0xf2, " REX_PRE "0xf, 0x38, 0xf1, 0xf1;"
			:"=S"(crc)
			:"0"(crc), "c"(*ptmp)
		);
		ptmp++;
	}

	if (iremainder) {
		crc = crc32c_hw_byte(crc, (unsigned char *)ptmp,
				 			iremainder);
	}

	return crc;
}


/* toggle x86 flag-register bits, as per mask
   return flags both in original and toggled state
 */
static void
toggle_x86_flags (long mask, long* orig, long* toggled)
{
	long forig = 0, fres = 0;

#if defined (__i386__)
	asm (
		"pushfl; popl %%eax; movl %%eax, %0; xorl %2, %%eax; "
		"pushl %%eax; popfl; pushfl; popl %%eax; pushl %0; popfl "
		: "=r" (forig), "=a" (fres)
		: "m" (mask)
	);
#elif __x86_64__
	asm (
		"pushfq; popq %%rax; movq %%rax, %0; xorq %2, %%rax; "
		"pushq %%rax; popfq; pushfq; popq %%rax; pushq %0; popfq "
		: "=r" (forig), "=a" (fres)
		: "m" (mask)
	);
#endif

	if (orig) 		*orig = forig;
	if (toggled) 	*toggled = fres;
	return;
}


/* is CPUID instruction available ? */
static int
can_cpuid ()
{
	long of = -1, tf = -1;

	/* x86 flag register masks */
	enum {
		cpuf_AC = (1 << 18), 	/* bit 18 */
		cpuf_ID = (1 << 21)		/* bit 21 */
	};


	/* check if AC (alignment) flag could be toggled:
		if not - it's i386, thus no CPUID
	*/
	toggle_x86_flags (cpuf_AC, &of, &tf);
	if ((of & cpuf_AC) == (tf & cpuf_AC)) {
		return 0;
	}

	/* next try toggling CPUID (ID) flag */
	toggle_x86_flags (cpuf_ID, &of, &tf);
	if ((of & cpuf_ID) == (tf & cpuf_ID)) {
		return 0;
	}

	return 1;
}


/* retrieve CPUID data using info as the EAX key */
static void
get_cpuid (long info, long* eax, long* ebx, long* ecx, long *edx)
{
	*eax = info;

#if defined (__i386__)
	asm __volatile__ (
		"movl %%ebx, %%edi; " 	/* must save ebx for 32-bit PIC code */
		"cpuid; "
		"movl %%ebx, %%esi; "
		"movl %%edi, %%ebx; "
		: "+a" (*eax), "=S" (*ebx), "=c" (*ecx), "=d" (*edx)
		:
		: "%edi"
	);
#elif defined (__x86_64__)
	asm __volatile__ (
		"cpuid; "
		: "+a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
	);
#endif
}


/* return 1=feature is available, 0=unavailable, 0>(errno) = error */
int
cpu_has (unsigned int feature)
{
	long info = 1, reg[4] = {0,0,0,0};

	if (!can_cpuid ())
		return -EINVAL;

	if (feature > LEN_cpu_ftr)
		return -ERANGE;

	get_cpuid (info, &reg[eAX], &reg[eBX], &reg[eCX], &reg[eDX]);

	return (reg[cpu_ftr[feature].ri] & cpu_ftr[feature].mask) ? 1 : 0;
}


/* __EOF__ */

