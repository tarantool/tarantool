#ifndef TARANTOOL_BOX_H_INCLUDED
#define TARANTOOL_BOX_H_INCLUDED
/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <mod/box/index.h>
#include "exception.h"
#include "iproto.h"
#include <tbuf.h>
#include <fiber.h>

struct tarantool_cfg;
struct box_tuple;

enum
{
	BOX_INDEX_MAX = 10,
	BOX_SPACE_MAX = 256,
	/** A limit on how many operations a single UPDATE can have. */
	BOX_UPDATE_OP_CNT_MAX = 128,
};

struct space {
	Index *index[BOX_INDEX_MAX];
	/** If not set (is 0), any tuple in the
	 * space can have any number of fields (but
	 * @sa max_fieldno). If set, Each tuple
	 * must have exactly this many fields.
	 */
	int arity;

	/**
	 * The number of indexes in the space.
	 *
	 * It is equal to the number of non-nil members of the index
	 * array and defines the key_defs array size as well.
	 */
	int key_count;

	/**
	 * The descriptors for all indexes that belong to the space.
	 */
	struct key_def *key_defs;

	/**
	 * Field types of indexed fields. This is an array of size
	 * field_count. If there are gaps, i.e. fields that do not
	 * participate in any index and thus we cannot infer their
	 * type, then respective array members have value UNKNOWN.
	 * XXX: right now UNKNOWN is also set for fields which types
	 * in two indexes contradict each other.
	 */
	enum field_data_type *field_types;

	/**
	 * Max field no which participates in any of the space indexes.
	 * Each tuple in this space must have, therefore, at least
	 * field_count fields.
	 */
	int max_fieldno;

	bool enabled;
};

extern struct space *space;

struct box_out {
	void (*add_u32)(u32 *u32);
	void (*dup_u32)(u32 u32);
	void (*add_tuple)(struct box_tuple *tuple);
};

extern struct box_out box_out_quiet;

struct box_txn {
	u16 op;
	u32 flags;

	struct lua_State *L;
	struct box_out *out;
	struct space *space;
	Index *index;
	int n;

	struct tbuf *ref_tuples;
	struct box_tuple *old_tuple;
	struct box_tuple *tuple;
	struct box_tuple *lock_tuple;

	struct tbuf req;
};


#define BOX_RETURN_TUPLE		0x01
#define BOX_ADD				0x02
#define BOX_REPLACE			0x04
#define BOX_NOT_STORE			0x10
#define BOX_GC_TXN			0x20
#define BOX_ALLOWED_REQUEST_FLAGS	(BOX_RETURN_TUPLE | \
					 BOX_ADD | \
					 BOX_REPLACE | \
					 BOX_NOT_STORE)

/*
    deprecated commands:
        _(INSERT, 1)
        _(DELETE, 2)
        _(SET_FIELD, 3)
        _(ARITH, 5)
        _(SET_FIELD, 6)
        _(ARITH, 7)
        _(SELECT, 4)
        _(DELETE, 8)
        _(UPDATE_FIELDS, 9)
        _(INSERT,10)
        _(JUBOX_ALIVE, 11)
        _(SELECT_LIMIT, 12)
        _(SELECT_OLD, 14)
        _(SELECT_LIMIT, 15)
        _(UPDATE_FIELDS_OLD, 16)

    DO NOT use these ids!
 */
#define MESSAGES(_)				\
        _(REPLACE, 13)				\
	_(SELECT, 17)				\
	_(UPDATE, 19)				\
	_(DELETE_1_3, 20)			\
	_(DELETE, 21)				\
	_(CALL, 22)

ENUM(messages, MESSAGES);

/** UPDATE operation codes. */
#define UPDATE_OP_CODES(_)			\
	_(UPDATE_OP_SET, 0)			\
	_(UPDATE_OP_ADD, 1)			\
	_(UPDATE_OP_AND, 2)			\
	_(UPDATE_OP_XOR, 3)			\
	_(UPDATE_OP_OR, 4)			\
	_(UPDATE_OP_SPLICE, 5)			\
	_(UPDATE_OP_DELETE, 6)			\
	_(UPDATE_OP_INSERT, 7)			\
	_(UPDATE_OP_NONE, 8)			\
	_(UPDATE_OP_MAX, 9)			\

ENUM(update_op_codes, UPDATE_OP_CODES);

extern iproto_callback rw_callback;

/**
 * Get space ordinal number.
 */
static inline int
space_n(struct space *sp)
{
	assert(sp >= space && sp < (space + BOX_SPACE_MAX));
	return sp - space;
}

/**
 * Get key_def ordinal number.
 */
static inline int
key_def_n(struct space *sp, struct key_def *kp)
{
	assert(kp >= sp->key_defs && kp < (sp->key_defs + sp->key_count));
	return kp - sp->key_defs;
}

/**
 * Get index ordinal number.
 */
static inline int
index_n(Index *index)
{
	return key_def_n(index->space, index->key_def);
}

static inline bool
index_is_primary(Index *index)
{
	return index_n(index) == 0;
}

/* These are used to implement memcached 'GET' */
static inline struct box_txn *in_txn() { return fiber->mod_data.txn; }
struct box_txn *txn_begin();
void txn_commit(struct box_txn *txn);
void txn_rollback(struct box_txn *txn);
void tuple_txn_ref(struct box_txn *txn, struct box_tuple *tuple);

#endif /* TARANTOOL_BOX_H_INCLUDED */
