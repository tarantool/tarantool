#ifndef INCLUDES_TARANTOOL_BOX_PHIA_H
#define INCLUDES_TARANTOOL_BOX_PHIA_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

void    *phia_env(void);
void    *phia_document(void*);
int      phia_setstring(void*, const char*, const void*, int);
int      phia_setint(void*, const char*, int64_t);
void    *phia_getobject(void*, const char*);
void    *phia_getstring(void*, const char*, int*);
int64_t  phia_getint(void*, const char*);
int      phia_open(void*);
int      phia_close(void*);
int      phia_drop(void*);
int      phia_destroy(void*);
int      phia_service(void*);
int      phia_set(void*, void*);
int      phia_upsert(void*, void*);
int      phia_delete(void*, void*);
void    *phia_get(void*, void*);
void    *phia_cursor(void*);
void    *phia_begin(void*);
int      phia_commit(void*);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_TARANTOOL_BOX_PHIA_H */
