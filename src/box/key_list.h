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
 * Function to prepare a value returned by
 * key_list_iterator_next method.
 */
typedef const char *(*key_list_allocator_t)(struct tuple *tuple, const char *key,
				       uint32_t key_sz);

/**
 * An iterator to iterate over the key_data returned by function
 * and validate it with given key definition (when required).
 */
struct key_list_iterator {
	/** The ancestor tuple. */
	struct tuple *tuple;
	/**
	 * The sequential functional index key definition that
	 * describes a format of functional index function keys.
	 */
	struct index_def *index_def;
	/** The pointer to currently processed key. */
	const char *data;
	/** The pointer to the end of extracted key_data. */
	const char *data_end;
	/** Whether iterator must validate processed keys. */
	bool validate;
	/** The method to allocate a key to be returned. */
	key_list_allocator_t key_allocator;
};

/**
 * Initialize a new functional index function returned
 * keys iterator.
 * Execute a function specified in a given functional index key
 * definition (a functional index function) and initialize a new
 * iterator on MsgPack array of with keys. Each key is a MsgPack
 * array as well.
 *
 * When validate flag is specified, processed keys are validated
 * to match given functional index key definition.
 *
 * Returns 0 in case of success, -1 otherwise.
 * Uses fiber region to allocate memory.
 */
int
key_list_iterator_create(struct key_list_iterator *it, struct tuple *tuple,
			 struct index_def *index_def, bool validate,
			 key_list_allocator_t key_allocator);

/**
 * Perform key iterator step and update iterator state.
 * Update key pointer with an actual key.
 *
 * Returns 0 on success. In case of error returns -1 and sets
 * the corresponding diag message.
 */
int
key_list_iterator_next(struct key_list_iterator *it, const char **value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TARANTOOL_BOX_KEY_LIST_H_INCLUDED */
