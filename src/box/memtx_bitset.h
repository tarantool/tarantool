#ifndef TARANTOOL_BOX_MEMTX_BITSET_H_INCLUDED
#define TARANTOOL_BOX_MEMTX_BITSET_H_INCLUDED
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

/**
 * @brief Index API wrapper for bitset_index
 * @see bitset/index.h
 */
#include "index.h"
#include "bitset/index.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef OLD_GOOD_BITSET
struct matras;
struct mh_bitset_index_t;
#endif /*#ifndef OLD_GOOD_BITSET*/

struct memtx_bitset_index {
	struct index base;
	struct bitset_index index;
#ifndef OLD_GOOD_BITSET
	struct matras *id_to_tuple;
	struct mh_bitset_index_t *tuple_to_id;
	uint32_t spare_id;
#endif /*#ifndef OLD_GOOD_BITSET*/
};

struct memtx_bitset_index *
memtx_bitset_index_new(struct index_def *);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_MEMTX_BITSET_H_INCLUDED */
