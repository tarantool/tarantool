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
#include <exceptions.h>
#include <tbuf.h>

@interface tnt_BoxException: tnt_Exception {
	@public
		u32 errcode;
}


- init:(const char *)p_file:(unsigned)p_line reason:(const char *)p_reason errcode:(u32)p_errcode;
- init:(const char *)p_file:(unsigned)p_line errcode:(u32)p_errcode;
@end

extern bool box_updates_allowed;
void memcached_handler(void * /* data */);

struct namespace;
struct box_tuple;
struct index;

extern struct index *memcached_index;

#define MAX_IDX 10
struct namespace {
	int n;
	bool enabled;
	int cardinality;
	struct index index[MAX_IDX];
};

extern struct namespace *namespace;
extern const int namespace_count;

struct box_tuple {
	u16 refs;
	u16 flags;
	u32 bsize;
	u32 cardinality;
	u8 data[0];
} __attribute__((packed));

struct box_txn {
	u16 op;
	u32 flags;

	struct namespace *namespace;
	struct index *index;
	int n;

	struct tbuf *ref_tuples;
	struct box_tuple *old_tuple;
	struct box_tuple *tuple;
	struct box_tuple *lock_tuple;

	size_t saved_iov_cnt;
	struct tbuf req;

	bool in_recover;
	bool write_to_wal;
};

enum tuple_flags {
	WAL_WAIT = 0x1,
	GHOST = 0x2,
	NEW = 0x4,
	SEARCH = 0x8
};

enum box_mode {
	RO = 1,
	RW
};

#define BOX_RETURN_TUPLE		0x01
#define BOX_ADD				0x02
#define BOX_REPLACE			0x04
#define BOX_QUIET			0x08
#define BOX_NOT_STORE			0x10
#define BOX_ALLOWED_REQUEST_FLAGS	(BOX_RETURN_TUPLE | \
					 BOX_ADD | \
					 BOX_REPLACE | \
					 BOX_QUIET)

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
        _(SELECT_LIMIT, 12)
        _(SELECT_OLD, 14)
        _(UPDATE_FIELDS_OLD, 16)
        _(JUBOX_ALIVE, 11)

    DO NOT use these ids!
 */
#define MESSAGES(_)				\
        _(INSERT, 13)				\
        _(SELECT_LIMIT, 15)			\
	_(SELECT, 17)				\
	_(UPDATE_FIELDS, 19)			\
	_(DELETE, 20)

ENUM(messages, MESSAGES);

struct box_txn *txn_alloc(u32 flags);
u32 box_process(struct box_txn *txn, u32 op, struct tbuf *request_data);

void tuple_txn_ref(struct box_txn *txn, struct box_tuple *tuple);
void txn_cleanup(struct box_txn *txn);

void *next_field(void *f);
void append_field(struct tbuf *b, void *f);
void *tuple_field(struct box_tuple *tuple, size_t i);

void memcached_init(void);
void memcached_expire(void * /* data */);
#endif /* TARANTOOL_BOX_H_INCLUDED */
