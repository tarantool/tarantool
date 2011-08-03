#if !defined(TARANTOOL_BOX_TUPLE_H_INCLUDED)
#define TARANTOOL_BOX_TUPLE_H_INCLUDED
/*
 * Copyright (C) 2011 Mail.RU
 * Copyright (C) 2011 Yuriy Vostrikov
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

#include <util.h>
#include <tbuf.h>

/**
 * tuple types definition
 */

/** tuple's flags */
enum tuple_flags {
	/** locked flag */
	WAL_WAIT = 0x1,
	/** tuple in the ghost mode. New primari key created but not
	    already commited to wal. */
	GHOST = 0x2,
};

/** box tuple */
struct box_tuple
{
	/** tuple reference cunter */
	u16 refs;
	/** flags */
	u16 flags;
	/** tuple size */
	u32 bsize;
	/** fields number in the tuple */
	u32 cardinality;
	/** fields */
	u8 data[0];
} __attribute__((packed));


/*
 * tuple interface declaraion
 */

/** Allocate tuple */
struct box_tuple *
tuple_alloc(size_t size);

/**
 * Clean-up tuple
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_free(struct box_tuple *tuple);

/**
 * Add count to tuple's reference counter. If tuple's refs counter down to
 * zero the tuple will be destroyed.
 *
 * @pre tuple->refs + count >= 0
 */
void
tuple_ref(struct box_tuple *tuple, int count);

/**
 * Get field from tuple
 *
 * @returns field data if field is exist or NULL
 */
void *
tuple_field(struct box_tuple *tuple, size_t i);

/**
 * Print a tuple in yaml-compatible mode tp tbuf:
 * key: { value, value, value }
 */
void
tuple_print(struct tbuf *buf, uint8_t cardinality, void *f);


#endif /* !defined(TARANTOOL_BOX_TUPLE_H_INCLUDED) */

