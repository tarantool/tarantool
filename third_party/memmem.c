/*	$NetBSD: memmem.c,v 1.2 2008/04/28 20:23:00 martin Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Perry E. Metzger of Metzger, Dowdeswell & Co. LLC.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <string.h>

void *
memmem(const void *block, size_t blen, const void *pat, size_t plen)
{
	const unsigned char *bp, *pp, *endp;

	assert(block != NULL);
	assert(pat != NULL);

	/*
	 * Following the precedent in ststr(3) and glibc, a zero
	 * length pattern matches the start of block.
	 */
	if (plen == 0)
		return (void*) block;

	if (blen < plen)
		return NULL;

	bp = block;
	pp = pat;
	endp = bp + (blen - plen) + 1;

	/*
	 * As a cheezy optimization, check that the first chars are
	 * the same before calling memcmp. Really we should use bm(3)
	 * to speed this up if blen is large enough.
	 */
	while (bp < endp) {
		if ((*bp == *pp) && (memcmp(bp, pp, plen) == 0))
			return (void *) bp;
		bp++;
	}

	return NULL;
}
