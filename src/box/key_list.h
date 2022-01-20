#ifndef TARANTOOL_BOX_KEY_LIST_H_INCLUDED
#define TARANTOOL_BOX_KEY_LIST_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stdbool.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct index_def;
struct tuple;

/**
 * We iterate over a functional index key list in two cases: when
 * adding a new tuple or removing the old one. When adding a new
 * tuple we need to copy functional index data to tuple memory, so
 * provide an allocator for that. When removing an old index, it's
 * fine to use a temporary memory area for the functional index
 * key, since the key is only used to lookup the old tuple in the
 * b+* tree, so we pass in a dummy allocator.
 */
typedef const char *(*key_list_allocator_t)(const char *key, uint32_t key_sz);

/**
 * An iterator over key_data returned by a stored function function.
 * Is used in two contexts:
 * When indexing a new tuple, to validate the key using the provided
 * key definition and copy it to tuple memory if validation succeeds.
 * When deleting an old tuple, to simply go over all keys, without
 * validation or copying.
 * Abstracts out multi-key and single-key functional indexes, i.e.
 * single-key functions simply return a list of keys of size 1.
 */
struct key_list_iterator {
	/** The tuple to supply to the function. */
	struct tuple *tuple;
	/**
	 * A functional index definition. We're mostly interested
	 * in index_def->key_def->func, but also need to know the
	 * space and index name to properly report errors.
	 */
	struct index_def *index_def;
	/** The pointer to the current key. */
	const char *data;
	/** The pointer to the end of the current key_data. */
	const char *data_end;
	/** Whether the iterator must validate processed keys. */
	bool validate;
	/** Allocate a key copy before returning it. */
	key_list_allocator_t key_allocator;
};

/**
 * Initialize a new key list iterator.
 *
 * Executes a function specified in the given functional index
 * definition and initializes a new iterator over the MsgPack
 * array with keys. Each key is a nested MsgPack array.
 *
 * When validate flag is set, each array entry is validated
 * to match the given functional index key definition.
 * Uses fiber region to allocate memory.
 *
 * @retval 0 in case of success
 * @retval -1 on function error, validation error, memory error.
 */
int
key_list_iterator_create(struct key_list_iterator *it, struct tuple *tuple,
			 struct index_def *index_def, bool validate,
			 key_list_allocator_t key_allocator);

/**
 * Return the next key and advance the iterator state.
 * If the iterator is exhausted, the value is set to 0.
 *
 * @retval 0 on success or EOF.
 * @retval -1 on error; diag is set.
 */
int
key_list_iterator_next(struct key_list_iterator *it, const char **value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TARANTOOL_BOX_KEY_LIST_H_INCLUDED */
