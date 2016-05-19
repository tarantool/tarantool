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
#include <stdbool.h>

struct phia_env;
struct phia_service;
struct phia_tx;
struct phia_document;
struct phia_cursor;
struct phia_index;

struct phia_env *
phia_env_new(void);

int
phia_env_open(struct phia_env *e);

int
phia_env_delete(struct phia_env *e);

struct phia_tx *
phia_begin(struct phia_env *e);

int
phia_replace(struct phia_tx *tx, struct phia_document *doc);

int
phia_upsert(struct phia_tx *tx, struct phia_document *doc);

int
phia_delete(struct phia_tx *tx, struct phia_document *doc);

int
phia_commit(struct phia_tx *tx);

int
phia_rollback(struct phia_tx *tx);

struct phia_document *
phia_document(struct phia_index *index);

int      phia_setstring(void*, const char*, const void*, int);
int      phia_setint(void*, const char*, int64_t);
void    *phia_getobject(void*, const char*);
void    *phia_getstring(void*, const char*, int*);
int64_t  phia_getint(void*, const char*);
int      phia_open(void*);
int      phia_drop(void*);
int      phia_destroy(void*);
struct phia_service *
phia_service_new(struct phia_env *env);
int
phia_service_do(struct phia_service *srv);
void
phia_service_delete(struct phia_service *srv);
void    *phia_get(void*, void*);

struct phia_cursor *
phia_cursor(struct phia_index *index);
void *
phia_confcursor(struct phia_env *env);
void
phia_cursor_delete(struct phia_cursor *cursor);
void
phia_cursor_set_read_commited(struct phia_cursor *cursor, bool read_commited);
struct phia_document *
phia_cursor_get(struct phia_cursor *cursor, struct phia_document *key);

struct phia_index *
phia_index_by_name(struct phia_env *env, const char *name);

int
phia_index_open(struct phia_index *index);

int
phia_index_close(struct phia_index *index);

int
phia_index_drop(struct phia_index *index);

int
phia_index_delete(struct phia_index *index);

struct phia_document *
phia_index_get(struct phia_index *index, struct phia_document *key);

void
phia_tx_set_lsn(struct phia_tx *tx, int64_t lsn);

void
phia_tx_set_half_commit(struct phia_tx *tx, bool half_commit);

struct phia_document *
phia_tx_get(struct phia_tx *t, struct phia_document *key);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_TARANTOOL_BOX_PHIA_H */
