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
#include "tuple_dictionary.h"
#include "error.h"
#include "diag.h"

#include "PMurHash.h"

field_name_hash_f field_name_hash;

#define mh_name _strnu32
struct mh_strnu32_key_t {
	const char *str;
	size_t len;
	uint32_t hash;
};
#define mh_key_t struct mh_strnu32_key_t *
struct mh_strnu32_node_t {
	const char *str;
	size_t len;
	uint32_t hash;
	uint32_t val;
};
#define mh_node_t struct mh_strnu32_node_t

#define mh_arg_t void *
#define mh_hash(a, arg) ((a)->hash)
#define mh_hash_key(a, arg) mh_hash(a, arg)
#define mh_cmp(a, b, arg) ((a)->len != (b)->len || \
			   memcmp((a)->str, (b)->str, (a)->len))
#define mh_cmp_key(a, b, arg) mh_cmp(a, b, arg)
#define MH_SOURCE 1
#include "salad/mhash.h" /* Create mh_strnu32_t hash. */

/** Free names hash and its content. */
static inline void
tuple_dictionary_delete_hash(struct mh_strnu32_t *hash)
{
	while (mh_size(hash)) {
		mh_int_t i = mh_first(hash);
		mh_strnu32_del(hash, i, NULL);
	}
	mh_strnu32_delete(hash);
}

/** Free tuple dictionary and its content. */
static inline void
tuple_dictionary_delete(struct tuple_dictionary *dict)
{
	assert(dict->refs == 0);
	if (dict->hash != NULL) {
		tuple_dictionary_delete_hash(dict->hash);
		free(dict->names);
	} else {
		assert(dict->names == NULL);
	}
	free(dict);
}

/**
 * Set a new name in a dictionary. Check duplicates. Memory must
 * be reserved already.
 * @param dict Tuple dictionary.
 * @param name New name.
 * @param name_len Length of @a name.
 * @param fieldno Field number.
 *
 * @retval  0 Success.
 * @retval -1 Duplicate name error.
 */
static inline int
tuple_dictionary_set_name(struct tuple_dictionary *dict, const char *name,
			  uint32_t name_len, uint32_t fieldno)
{
	assert(fieldno < dict->name_count);
	uint32_t name_hash = field_name_hash(name, name_len);
	struct mh_strnu32_key_t key = {
		name, name_len, name_hash
	};
	mh_int_t rc = mh_strnu32_find(dict->hash, &key, NULL);
	if (rc != mh_end(dict->hash)) {
		diag_set(ClientError, ER_SPACE_FIELD_IS_DUPLICATE,
			 name);
		return -1;
	}
	struct mh_strnu32_node_t name_node = {
		name, name_len, name_hash, fieldno
	};
	rc = mh_strnu32_put(dict->hash, &name_node, NULL, NULL);
	/* Memory was reserved in new(). */
	assert(rc != mh_end(dict->hash));
	(void) rc;
	return 0;
}

struct tuple_dictionary *
tuple_dictionary_new(const struct field_def *fields, uint32_t field_count)
{
	struct tuple_dictionary *dict =
		(struct tuple_dictionary *)calloc(1, sizeof(*dict));
	if (dict == NULL) {
		diag_set(OutOfMemory, sizeof(*dict), "malloc",
			 "dict");
		return NULL;
	}
	dict->refs = 1;
	dict->name_count = field_count;
	if (field_count == 0)
		return dict;
	uint32_t names_offset = sizeof(dict->names[0]) * field_count;
	uint32_t total = names_offset;
	for (uint32_t i = 0; i < field_count; ++i)
		total += strlen(fields[i].name) + 1;
	dict->names = (char **) malloc(total);
	if (dict->names == NULL) {
		diag_set(OutOfMemory, total, "malloc", "dict->names");
		goto err_memory;
	}
	dict->hash = mh_strnu32_new();
	if (dict->hash == NULL) {
		diag_set(OutOfMemory, sizeof(*dict->hash),
			 "mh_strnu32_new", "dict->hash");
		goto err_hash;
	}
	if (mh_strnu32_reserve(dict->hash, field_count, NULL) != 0) {
		diag_set(OutOfMemory, field_count *
			 sizeof(struct mh_strnu32_node_t), "mh_strnu32_reserve",
			 "dict->hash");
		goto err_name;
	}
	char *pos = (char *) dict->names + names_offset;
	for (uint32_t i = 0; i < field_count; ++i) {
		int len = strlen(fields[i].name);
		memcpy(pos, fields[i].name, len);
		pos[len] = 0;
		dict->names[i] = pos;
		if (tuple_dictionary_set_name(dict, pos, len, i) != 0)
			goto err_name;
		pos += len + 1;
	}
	return dict;

err_name:
	tuple_dictionary_delete_hash(dict->hash);
err_hash:
	free(dict->names);
err_memory:
	free(dict);
	return NULL;
}

uint32_t
tuple_dictionary_hash_process(const struct tuple_dictionary *dict,
			      uint32_t *ph, uint32_t *pcarry)
{
	uint32_t size = 0;
	for (uint32_t i = 0; i < dict->name_count; ++i) {
		uint32_t name_len = strlen(dict->names[i]);
		PMurHash32_Process(ph, pcarry, dict->names[i], name_len);
		size += name_len;
	}
	return size;
}

int
tuple_dictionary_cmp(const struct tuple_dictionary *a,
		     const struct tuple_dictionary *b)
{
	if (a->name_count != b->name_count)
		return a->name_count > b->name_count ? 1 : -1;
	for (uint32_t i = 0; i < a->name_count; ++i) {
		int ret = strcmp(a->names[i], b->names[i]);
		if (ret != 0)
			return ret;
	}
	return 0;
}

void
tuple_dictionary_swap(struct tuple_dictionary *a, struct tuple_dictionary *b)
{
	int a_refs = a->refs;
	int b_refs = b->refs;
	struct tuple_dictionary t = *a;
	*a = *b;
	*b = t;
	a->refs = a_refs;
	b->refs = b_refs;
}

void
tuple_dictionary_unref(struct tuple_dictionary *dict)
{
	assert(dict->refs > 0);
	if (--dict->refs == 0)
		tuple_dictionary_delete(dict);
}

void
tuple_dictionary_ref(struct tuple_dictionary *dict)
{
	++dict->refs;
}

int
tuple_fieldno_by_name(struct tuple_dictionary *dict, const char *name,
		      uint32_t name_len, uint32_t name_hash, uint32_t *fieldno)
{
	struct mh_strnu32_t *hash = dict->hash;
	if (hash == NULL)
		return -1;
	struct mh_strnu32_key_t key = {name, name_len, name_hash};
	mh_int_t rc = mh_strnu32_find(hash, &key, NULL);
	if (rc == mh_end(hash))
		return -1;
	*fieldno = mh_strnu32_node(hash, rc)->val;
	return 0;
}
