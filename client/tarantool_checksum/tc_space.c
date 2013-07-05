
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

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include "tc_key.h"
#include "tc_hash.h"
#include "tc_options.h"
#include "tc_space.h"

int tc_space_init(struct tc_spaces *s) {
	s->t = mh_u32ptr_new();
	if (s->t == NULL)
		return -1;
	return 0;
}

void tc_space_free(struct tc_spaces *s)
{
	while (mh_size(s->t) > 0) {
		mh_int_t i = mh_first(s->t);

		struct tc_space *space = mh_u32ptr_node(s->t, i)->val;
		mh_u32ptr_del(s->t, i, NULL);

		mh_int_t pos = 0;
		while (pos != mh_end(space->hash_log)) {
			if (mh_exist((space->hash_log), pos)) {
				struct tc_key *k =
					*mh_pk_node(space->hash_log, pos);
				free(k);
			}
			pos++;
		}
		pos = 0;
		while (pos != mh_end(space->hash_snap)) {
			if (mh_exist((space->hash_snap), pos)) {
				struct tc_key *k =
					*mh_pk_node(space->hash_snap, pos);
				free(k);
			}
			pos++;
		}

		mh_pk_delete(space->hash_log);
		mh_pk_delete(space->hash_snap);

		free(space->pk.fields);
		free(space);
	}
	mh_u32ptr_delete(s->t);
}

struct tc_space *tc_space_create(struct tc_spaces *s, uint32_t id) {
	struct tc_space *space = malloc(sizeof(struct tc_space));
	if (space == NULL)
		return NULL;
	memset(space, 0, sizeof(struct tc_space));
	space->id = id;
	space->hash_log = mh_pk_new();
	space->hash_snap = mh_pk_new();

	const struct mh_u32ptr_node_t node = { .key = space->id, .val = space };
	mh_u32ptr_put(s->t, &node, NULL, space);
	return space;
}

struct tc_space *tc_space_match(struct tc_spaces *s, uint32_t id) {
	const struct mh_u32ptr_node_t node = { .key = id };
	mh_int_t k = mh_u32ptr_get(s->t, &node, NULL);
	struct tc_space *space = NULL;
	if (k != mh_end(s->t))
		space = mh_u32ptr_node(s->t, k)->val;
	return space;
}

enum tc_space_key_type
tc_space_key_typeof(char *name)
{
	if (strcmp(name, "NUM")  == 0)
		return TC_SPACE_KEY_NUM;
	else
	if (strcmp(name, "NUM64")  == 0)
		return TC_SPACE_KEY_NUM64;
	else
	if (strcmp(name, "STR")  == 0)
		return TC_SPACE_KEY_STRING;
	return TC_SPACE_KEY_UNKNOWN;
}

static int
tc_space_key_init(struct tc_space *s, tarantool_cfg_space *cs)
{
	struct tarantool_cfg_space_index *primary = cs->index[0];

	/* calculate primary key part count */
	while (primary->key_field[s->pk.count]) {
		tarantool_cfg_space_index_key_field *ck = primary->key_field[s->pk.count];
		if (ck->fieldno == -1)
			break;
		s->pk.count++;
	}

	/* allocate key fields */
	size_t size = sizeof(struct tc_space_key_field) * s->pk.count;
	s->pk.fields = malloc(size);
	if (s->pk.fields == NULL) {
		printf("can't allocate key fields\n");
		return -1;
	}
	memset(s->pk.fields, 0, size);

	/* init key fields */
	int kn = 0;
	while (primary->key_field[kn]) {
		struct tc_space_key_field *k = &s->pk.fields[kn];
		tarantool_cfg_space_index_key_field *ck = primary->key_field[kn];
		if (ck->fieldno == -1)
			break;
		k->n = ck->fieldno;
		k->type = tc_space_key_typeof(ck->type);
		kn++;
	}

	return 0;
}

int tc_space_fillof(struct tc_spaces *s, int n, tarantool_cfg_space *cs)
{
	struct tc_space *space = tc_space_match(s, n);
	if (space) {
		printf("space %i is already defined\n", n);
		return -1;
	}
	space = tc_space_create(s, n);
	if (space == NULL) {
		printf("failed to create space %d\n", n);
		return -1;
	}
	if (cs->index[0] == NULL) {
		printf("primary index is not defined\n");
		return -1;
	}
	memset(&space->pk, 0, sizeof(space->pk));

	int rc = tc_space_key_init(space, cs);
	if (rc == -1)
		return -1;
	return 0;
}

int tc_space_fill(struct tc_spaces *s, struct tc_options *opts)
{
	int i = 0;
	for (; opts->cfg.space[i]; i++) {
		tarantool_cfg_space *cs = opts->cfg.space[i];
		if (!CNF_STRUCT_DEFINED(cs) || !cs->enabled)
			continue;
		int rc = tc_space_fillof(s, i, cs);
		if (rc == -1)
			return -1;
	}
	return 0;
}
