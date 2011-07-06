
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

#include <tnt_error.h>
#include <tnt_mem.h>

static void *(*_tnt_malloc)(int size) = (void *(*)(int))malloc;
static void *(*_tnt_realloc)(void *ptr, int size) =
	(void *(*)(void*, int))realloc;
static void *(*_tnt_dup)(char *sz) = (void *(*)(char*))strdup;
static void (*_tnt_free)(void *ptr) = (void (*)(void*))free;

void
tnt_mem_init(void *(*malloc_)(int size),
	     void *(*realloc_)(void *ptr, int size),
	     void *(*dup_)(char *sz),
	     void (*free_)(void *ptr))
{
	if (malloc_)
		_tnt_malloc = malloc_;
	if (realloc_)
		_tnt_realloc = realloc_;
	if (dup_)
		_tnt_dup = dup_;
	if (free_)
		_tnt_free = free_;
}

void*
tnt_mem_alloc(int size)
{
	return _tnt_malloc(size);
}

void*
tnt_mem_realloc(void *ptr, int size)
{
	return _tnt_realloc(ptr, size);
}

char*
tnt_mem_dup(char *sz)
{
	return _tnt_dup(sz);
}

void
tnt_mem_free(void *ptr)
{
	_tnt_free(ptr);
}
