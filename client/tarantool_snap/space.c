
/*
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include "key.h"
#include "hash.h"
#include "options.h"
#include "space.h"
#include "sha1.h"

int ts_space_init(struct ts_spaces *s) {
	s->t = mh_u32ptr_new();
	if (s->t == NULL)
		return -1;
	return 0;
}

void ts_space_free(struct ts_spaces *s)
{
	return;
	mh_int_t i;
	mh_foreach(s->t, i) {
		struct ts_space *space = mh_u32ptr_node(s->t, i)->val;
		mh_u32ptr_del(s->t, i, NULL);

		mh_int_t pos = 0;
		while (pos != mh_end(space->index)) {
			if (mh_exist((space->index), pos)) {
				struct ts_key *k =
					*mh_pk_node(space->index, pos);
				free(k);
			}
			pos++;
		}

		mh_pk_delete(space->index);
		free(space->pk.fields);
		free(space);
	}
	mh_u32ptr_delete(s->t);
}

struct ts_space *ts_space_create(struct ts_spaces *s, uint32_t id) {
	struct ts_space *space = malloc(sizeof(struct ts_space));
	if (space == NULL)
		return NULL;
	memset(space, 0, sizeof(struct ts_space));
	space->id = id;
	space->index = mh_pk_new();

	const struct mh_u32ptr_node_t node = { .key = space->id, .val = space };
	mh_u32ptr_put(s->t, &node, NULL, space);
	return space;
}

struct ts_space *ts_space_match(struct ts_spaces *s, uint32_t id) {
	const struct mh_u32ptr_node_t node = { .key = id };
	mh_int_t k = mh_u32ptr_get(s->t, &node, NULL);
	struct ts_space *space = NULL;
	if (k != mh_end(s->t))
		space = mh_u32ptr_node(s->t, k)->val;
	return space;
}

enum ts_space_key_type
ts_space_key_typeof(char *name)
{
	if (strcmp(name, "NUM") == 0)
		return TS_SPACE_KEY_NUM;
	else
	if (strcmp(name, "NUM64") == 0)
		return TS_SPACE_KEY_NUM64;
	else
	if (strcmp(name, "STR") == 0)
		return TS_SPACE_KEY_STRING;
	return TS_SPACE_KEY_UNKNOWN;
}

static int
ts_space_key_init(struct ts_space *s, tarantool_cfg_space *cs)
{
	struct tarantool_cfg_space_index *primary = cs->index[0];

	/* calculate primary key part count */
	while (primary->key_field[s->pk.count]) {
		struct tarantool_cfg_space_index_key_field *ck =
		   	primary->key_field[s->pk.count];
			/*typeof(primary->key_field[s->pk.count]) ck = primary->key_field[s->pk.count];*/
		if (ck->fieldno == -1)
			break;
		s->pk.count++;
	}

	/* allocate key fields */
	size_t size = sizeof(struct ts_space_key_field) * s->pk.count;
	s->pk.fields = malloc(size);
	if (s->pk.fields == NULL) {
		printf("can't allocate key fields\n");
		return -1;
	}
	memset(s->pk.fields, 0, size);

	int key_has_string = 0;
	int key_size = 0;

	/* init key fields */
	int kn = 0;
	while (primary->key_field[kn]) {
		struct ts_space_key_field *k = &s->pk.fields[kn];
		struct tarantool_cfg_space_index_key_field *ck = primary->key_field[kn];
		/*typeof(primary->key_field[s->pk.count]) ck = primary->key_field[kn];*/
		if (ck->fieldno == -1)
			break;
		k->n = ck->fieldno;
		k->type = ts_space_key_typeof(ck->type);
		if (key_has_string) {
			kn++;
			continue;
		}
		switch (k->type) {
		case TS_SPACE_KEY_STRING:
			key_has_string = 1;
			break;
		case TS_SPACE_KEY_NUM:
			key_size += 4;
			break;
		case TS_SPACE_KEY_NUM64:
			key_size += 8;
			break;
		default:
			printf("bad key type\n");
			return -1;
		}
		kn++;
	}

	/* decide key compaction type */
	if (key_has_string || key_size > 20) {
		s->c = TS_SPACE_COMPACT_CHECKSUM;
		s->key_size = 20;
		s->key_div = 5;
	} else {
		s->c = TS_SPACE_COMPACT_SPARSE;
		s->key_size = key_size;
		s->key_div = key_size / 4;
	}

	return 0;
}

int ts_space_fillof(struct ts_spaces *s, int n, tarantool_cfg_space *cs)
{
	struct ts_space *space = ts_space_match(s, n);
	if (space) {
		printf("space %i is already defined\n", n);
		return -1;
	}
	space = ts_space_create(s, n);
	if (space == NULL) {
		printf("failed to create space %d\n", n);
		return -1;
	}
	if (cs->index[0] == NULL) {
		printf("primary index is not defined\n");
		return -1;
	}
	memset(&space->pk, 0, sizeof(space->pk));

	int rc = ts_space_key_init(space, cs);
	if (rc == -1)
		return -1;
	return 0;
}

int ts_space_fill(struct ts_spaces *s, struct ts_options *opts)
{
	int i = 0;
	for (; opts->cfg.space[i]; i++) {
		tarantool_cfg_space *cs = opts->cfg.space[i];
		if (!CNF_STRUCT_DEFINED(cs) || !cs->enabled)
			continue;
		int rc = ts_space_fillof(s, i, cs);
		if (rc == -1)
			return -1;
	}
	return 0;
}

static inline struct ts_key*
ts_space_keyalloc_sha(struct ts_space *s, struct tnt_tuple *t, int fileid,
                      int offset, int attach)
{
	int size = sizeof(struct ts_key) + s->key_size;
	if (attach)
		size += sizeof(uint32_t) + t->size;
	struct ts_key *k = malloc(size);
	if (k == NULL)
		return NULL;
	k->file = fileid;
	k->offset = offset;
	k->flags = 0;

	SHA1_CTX ctx;
	SHA1Init(&ctx);
	int i = 0;
	while (i < s->pk.count) {
		struct tnt_iter it;
		tnt_iter(&it, t);
		if (tnt_field(&it, t, s->pk.fields[i].n) == NULL) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
		if (it.status != TNT_ITER_OK) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
        	SHA1Update(&ctx, (const unsigned char*)TNT_IFIELD_DATA(&it),
		           TNT_IFIELD_SIZE(&it));
		tnt_iter_free(&it);
		i++;
	}
	SHA1Final(k->key, &ctx);

	if (attach) {
		k->flags = TS_KEY_WITH_DATA;
		memcpy(k->key + s->key_size, &t->size, sizeof(uint32_t));
		memcpy(k->key + s->key_size + sizeof(uint32_t), t->data, t->size);
	}
	return k;
}

static inline struct ts_key*
ts_space_keyalloc_sparse(struct ts_space *s, struct tnt_tuple *t, int fileid,
                         int offset, int attach)
{
	int size = sizeof(struct ts_key) + s->key_size;
	if (attach)
		size += sizeof(uint32_t) + t->size;
	struct ts_key *k = malloc(size);
	if (k == NULL)
		return NULL;
	k->file = fileid;
	k->offset = offset;
	k->flags = 0;

	int off = 0;
	int i = 0;
	while (i < s->pk.count) {
		struct tnt_iter it;
		tnt_iter(&it, t);
		if (tnt_field(&it, t, s->pk.fields[i].n) == NULL) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
		if (it.status != TNT_ITER_OK) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
		memcpy(k->key + off, TNT_IFIELD_DATA(&it), TNT_IFIELD_SIZE(&it));
		off += TNT_IFIELD_SIZE(&it);

		tnt_iter_free(&it);
		i++;
	}
	if (attach) {
		k->flags = TS_KEY_WITH_DATA;
		memcpy(k->key + s->key_size, &t->size, sizeof(uint32_t));
		memcpy(k->key + s->key_size + sizeof(uint32_t), t->data, t->size);
	}
	return k;
}

struct ts_key*
ts_space_keyalloc(struct ts_space *s, struct tnt_tuple *t, int fileid,
                  int offset, int attach)
{
	switch (s->c) {
	case TS_SPACE_COMPACT_CHECKSUM:
		return ts_space_keyalloc_sha(s, t, fileid, offset, attach);
	case TS_SPACE_COMPACT_SPARSE:
		return ts_space_keyalloc_sparse(s, t, fileid, offset, attach);
	}
	return NULL;
}
