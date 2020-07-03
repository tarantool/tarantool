#ifndef TARANTOOL_LIB_COLL_COLL_H_INCLUDED
#define TARANTOOL_LIB_COLL_COLL_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "coll_def.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct coll;

typedef int (*coll_cmp_f)(const char *s, size_t s_len, const char *t,
			  size_t t_len, const struct coll *coll);

typedef uint32_t (*coll_hash_f)(const char *s, size_t s_len, uint32_t *ph,
				uint32_t *pcarry, struct coll *coll);

typedef size_t (*coll_hint_f)(const char *s, size_t s_len, char *buf,
			      size_t buf_len, struct coll *coll);

struct UCollator;

/** Default universal casemap for case transformations. */
extern struct UCaseMap *icu_ucase_default_map;
/** Default universal utf8 converter. */
extern struct UConverter *icu_utf8_conv;

/**
 * Collation. It has no unique features like name, id or owner.
 * Only functional part - comparator, locale, ICU settings.
 */
struct coll {
	/** Collation type. */
	enum coll_type type;
	/** ICU collation specific data. */
	struct UCollator *collator;
	/** String comparator. */
	coll_cmp_f cmp;
	/** String hash function. */
	coll_hash_f hash;
	/**
	 * String comparison hint.
	 *
	 * This function copies a sort key for a string to
	 * the given buffer and returns the number of bytes
	 * copied. Sort keys may be compared using strcmp().
	 */
	coll_hint_f hint;
	/** Reference counter. */
	int refs;
	/**
	 * Formatted string with collation properties, that
	 * completely describes how the collation works.
	 */
	const char fingerprint[0];
};

/**
 * Return true if a key part using the second collation can be
 * merged into a key def using the first collation for the same
 * field. Using the two collations together for the same field
 * in the same key def only makes sense if the second collation
 * may impose a strict order on keys equal in terms of the first
 * collation.
 */
bool
coll_can_merge(const struct coll *first, const struct coll *second);

/**
 * Create a collation by definition. Can return an existing
 * collation object, if a one with the same fingerprint was
 * created before.
 * @param def Collation definition.
 * @retval NULL Collation or memory error.
 * @retval not NULL Collation.
 */
struct coll *
coll_new(const struct coll_def *def);

/** Increment reference counter. */
static inline void
coll_ref(struct coll *coll)
{
	++coll->refs;
}

/** Decrement reference counter. Delete when 0. */
void
coll_unref(struct coll *coll);

/** Initialize collations subsystem. */
void
coll_init(void);

/** Destroy collations subsystem. */
void
coll_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_COLL_COLL_H_INCLUDED */
