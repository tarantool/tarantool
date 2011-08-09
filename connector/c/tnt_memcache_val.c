
/*
 * Copyright (C) 2011 Mail.RU
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/uio.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_memcache_val.h>

void
tnt_memcache_val_init(struct tnt_memcache_vals *values)
{
	values->count = 0;
	values->values = NULL;
}

void
tnt_memcache_val_free(struct tnt_memcache_vals *values)
{
	unsigned int i;
	for (i = 0 ; i < values->count ; i++) {
		if (values->values[i].key)
			tnt_mem_free(values->values[i].key);
		if (values->values[i].value)
			tnt_mem_free(values->values[i].value);
	}
	if (values->values)
		tnt_mem_free(values->values);
}

int
tnt_memcache_val_alloc(struct tnt_memcache_vals *values, int count)
{
	values->values = tnt_mem_alloc(sizeof(struct tnt_memcache_val) * count);
	if (values->values == NULL)
		return -1;
	memset(values->values, 0, sizeof(struct tnt_memcache_val) * count);
	values->count = count;
	return 0;
}
