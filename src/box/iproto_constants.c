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
#include "iproto_constants.h"

#define bit(c) (1ULL<<IPROTO_##c)
const uint64_t iproto_body_key_map[IPROTO_TYPE_STAT_MAX] = {
	0,                                                     /* unused */
	bit(SPACE_ID) | bit(LIMIT) | bit(KEY),                 /* SELECT */
	bit(SPACE_ID) | bit(TUPLE),                            /* INSERT */
	bit(SPACE_ID) | bit(TUPLE),                            /* REPLACE */
	bit(SPACE_ID) | bit(KEY) | bit(TUPLE),                 /* UPDATE */
	bit(SPACE_ID) | bit(KEY),                              /* DELETE */
	0,                                                     /* CALL_16 */
	0,                                                     /* AUTH */
	0,                                                     /* EVAL */
	bit(SPACE_ID) | bit(OPS) | bit(TUPLE),                 /* UPSERT */
	0,                                                     /* CALL */
	0,                                                     /* EXECUTE */
	0,                                                     /* NOP */
	0,                                                     /* PREPARE */
	0,                                                     /* BEGIN */
	0,                                                     /* COMMIT */
	0,                                                     /* ROLLBACK */
};
#undef bit

#define IPROTO_FLAG_BIT_STRS_MEMBER(s, ...) \
	[IPROTO_FLAG_BIT_ ## s] = #s,

const char *iproto_flag_bit_strs[iproto_flag_bit_MAX] = {
	IPROTO_FLAGS(IPROTO_FLAG_BIT_STRS_MEMBER)
};

#define IPROTO_KEY_TYPE_MEMBER(s, v, t) \
	[IPROTO_ ## s] = t,

const unsigned char iproto_key_type[iproto_key_MAX] = {
	IPROTO_KEYS(IPROTO_KEY_TYPE_MEMBER)
};

#define IPROTO_KEY_STRS_MEMBER(s, ...) \
	[IPROTO_ ## s] = #s,

const char *iproto_key_strs[iproto_key_MAX] = {
	IPROTO_KEYS(IPROTO_KEY_STRS_MEMBER)
};

#define IPROTO_METADATA_KEY_STRS_MEMBER(s, ...) \
	[IPROTO_FIELD_ ## s] = #s,

const char *iproto_metadata_key_strs[iproto_metadata_key_MAX] = {
	IPROTO_METADATA_KEYS(IPROTO_METADATA_KEY_STRS_MEMBER)
};

#define IPROTO_BALLOT_KEY_STRS_MEMBER(s, ...) \
	[IPROTO_BALLOT_ ## s] = #s,

const char *iproto_ballot_key_strs[iproto_ballot_key_MAX] = {
	IPROTO_BALLOT_KEYS(IPROTO_BALLOT_KEY_STRS_MEMBER)
};

#define IPROTO_TYPE_STRS_MEMBER(s, ...) \
	[IPROTO_ ## s] = #s,

const char *iproto_type_strs[iproto_type_MAX] = {
	IPROTO_TYPES(IPROTO_TYPE_STRS_MEMBER)
};

char *iproto_type_lower_strs[iproto_type_MAX];

#define IPROTO_RAFT_KEY_STRS_MEMBER(s, ...) \
	[IPROTO_RAFT_ ## s] = #s,

const char *iproto_raft_key_strs[iproto_raft_key_MAX] = {
	IPROTO_RAFT_KEYS(IPROTO_RAFT_KEY_STRS_MEMBER)
};

#define VY_RUN_INFO_KEY_STRS_MEMBER(s, ...) \
	[VY_RUN_INFO_ ## s] = #s,

const char *vy_run_info_key_strs[vy_run_info_key_MAX] = {
	VY_RUN_INFO_KEYS(VY_RUN_INFO_KEY_STRS_MEMBER)
};

#define VY_PAGE_INFO_KEY_STRS_MEMBER(s, ...) \
	[VY_PAGE_INFO_ ## s] = #s,

const char *vy_page_info_key_strs[vy_page_info_key_MAX] = {
	VY_PAGE_INFO_KEYS(VY_PAGE_INFO_KEY_STRS_MEMBER)
};

#define VY_ROW_INDEX_KEY_STRS_MEMBER(s, ...) \
	[VY_ROW_INDEX_ ## s] = #s,

const char *vy_row_index_key_strs[vy_row_index_key_MAX] = {
	VY_ROW_INDEX_KEYS(VY_ROW_INDEX_KEY_STRS_MEMBER)
};
