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

const char *iproto_type_strs[] =
{
	NULL,
	"SELECT",
	"INSERT",
	"REPLACE",
	"UPDATE",
	"DELETE",
	NULL, /* CALL_16 */
	"AUTH",
	"EVAL",
	"UPSERT",
	"CALL",
	"EXECUTE",
	NULL, /* NOP */
	"PREPARE",
	"BEGIN",
	"COMMIT",
	"ROLLBACK",
};

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

const char *iproto_key_strs[iproto_key_MAX] = {
	"type",             /* 0x00 */
	"sync",             /* 0x01 */
	"replica id",       /* 0x02 */
	"lsn",              /* 0x03 */
	"timestamp",        /* 0x04 */
	"schema version",   /* 0x05 */
	"server version",   /* 0x06 */
	"group id",         /* 0x07 */
	"tsn",              /* 0x08 */
	"flags",            /* 0x09 */
	"stream_id",        /* 0x0a */
	NULL,               /* 0x0b */
	NULL,               /* 0x0c */
	NULL,               /* 0x0d */
	NULL,               /* 0x0e */
	NULL,               /* 0x0f */
	"space id",         /* 0x10 */
	"index id",         /* 0x11 */
	"limit",            /* 0x12 */
	"offset",           /* 0x13 */
	"iterator",         /* 0x14 */
	"index base",       /* 0x15 */
	NULL,               /* 0x16 */
	NULL,               /* 0x17 */
	NULL,               /* 0x18 */
	NULL,               /* 0x19 */
	NULL,               /* 0x1a */
	NULL,               /* 0x1b */
	NULL,               /* 0x1c */
	NULL,               /* 0x1d */
	NULL,               /* 0x1e */
	NULL,               /* 0x1f */
	"key",              /* 0x20 */
	"tuple",            /* 0x21 */
	"function name",    /* 0x22 */
	"user name",        /* 0x23 */
	"instance uuid",    /* 0x24 */
	"replicaset uuid",  /* 0x25 */
	"vector clock",     /* 0x26 */
	"expression",       /* 0x27 */
	"operations",       /* 0x28 */
	"ballot",           /* 0x29 */
	"tuple meta",       /* 0x2a */
	"options",          /* 0x2b */
	"old tuple",        /* 0x2c */
	"new tuple",        /* 0x2d */
	NULL,               /* 0x2e */
	NULL,               /* 0x2f */
	"data",             /* 0x30 */
	"error_24",         /* 0x31 */
	"metadata",         /* 0x32 */
	"bind meta",        /* 0x33 */
	"bind count",       /* 0x34 */
	NULL,               /* 0x35 */
	NULL,               /* 0x36 */
	NULL,               /* 0x37 */
	NULL,               /* 0x38 */
	NULL,               /* 0x39 */
	NULL,               /* 0x3a */
	NULL,               /* 0x3b */
	NULL,               /* 0x3c */
	NULL,               /* 0x3d */
	NULL,               /* 0x3e */
	NULL,               /* 0x3f */
	"SQL text",         /* 0x40 */
	"SQL bind",         /* 0x41 */
	"SQL info",         /* 0x42 */
	"stmt id",          /* 0x43 */
	NULL,               /* 0x44 */
	NULL,               /* 0x45 */
	NULL,               /* 0x46 */
	NULL,               /* 0x47 */
	NULL,               /* 0x48 */
	NULL,               /* 0x49 */
	NULL,               /* 0x4a */
	NULL,               /* 0x4b */
	NULL,               /* 0x4c */
	NULL,               /* 0x4d */
	NULL,               /* 0x4e */
	NULL,               /* 0x4f */
	"replica anon",     /* 0x50 */
	"id filter",        /* 0x51 */
	"error",            /* 0x52 */
	"term",             /* 0x53 */
	"version",          /* 0x54 */
	"features",         /* 0x55 */
	"timeout",          /* 0x56 */
	"event key",        /* 0x57 */
	"event data",       /* 0x58 */
	"txn isolation",    /* 0x59 */
};

const char *vy_page_info_key_strs[VY_PAGE_INFO_KEY_MAX] = {
	NULL,
	"offset",
	"size",
	"unpacked size",
	"row count",
	"min key",
	"row index offset"
};

const char *vy_run_info_key_strs[VY_RUN_INFO_KEY_MAX] = {
	NULL,
	"min key",
	"max key",
	"min lsn",
	"max lsn",
	"page count",
	"bloom filter legacy",
	"bloom filter",
	"stmt stat",
};

const char *vy_row_index_key_strs[VY_ROW_INDEX_KEY_MAX] = {
	NULL,
	"row index",
};

const struct iproto_constant iproto_flag_constants[] = {
	IPROTO_FLAGS(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_flag_constants_size = lengthof(iproto_flag_constants);

const struct iproto_constant iproto_key_constants[] = {
	IPROTO_KEYS(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_key_constants_size = lengthof(iproto_key_constants);

#define IPROTO_KEY_TYPE_MEMBER(s, v, t) \
	[IPROTO_ ## s] = t,

const unsigned char iproto_key_type[iproto_key_MAX] = {
	IPROTO_KEYS(IPROTO_KEY_TYPE_MEMBER)
};

const struct iproto_constant iproto_metadata_key_constants[] = {
	IPROTO_METADATA_KEYS(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_metadata_key_constants_size =
	lengthof(iproto_metadata_key_constants);

const struct iproto_constant iproto_ballot_key_constants[] = {
	IPROTO_BALLOT_KEYS(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_ballot_key_constants_size =
	lengthof(iproto_ballot_key_constants);

const struct iproto_constant iproto_type_constants[] = {
	IPROTO_TYPES(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_type_constants_size = lengthof(iproto_type_constants);

const struct iproto_constant iproto_raft_keys_constants[] = {
	IPROTO_RAFT_KEYS(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_raft_keys_constants_size =
	lengthof(iproto_raft_keys_constants);
