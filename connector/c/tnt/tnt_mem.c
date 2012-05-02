
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

#include <connector/c/include/tarantool/tnt_mem.h>

static void *(*_tnt_realloc)(void *ptr, size_t size) =
	(void *(*)(void*, size_t))realloc;

void *tnt_mem_init(tnt_allocator_t alloc) {
	void *ptr = _tnt_realloc;
	if (alloc)
		_tnt_realloc = alloc;
	return ptr;
}

void *tnt_mem_alloc(size_t size) {
	return _tnt_realloc(NULL, size);
}

void *tnt_mem_realloc(void *ptr, size_t size) {
	return _tnt_realloc(ptr, size);
}

char *tnt_mem_dup(char *sz) {
	size_t len = strlen(sz);
	char *szp = tnt_mem_alloc(len + 1);
	if (szp == NULL)
		return NULL;
	memcpy(szp, sz, len + 1);
	return szp;
}

void tnt_mem_free(void *ptr) {
	_tnt_realloc(ptr, 0);
}
