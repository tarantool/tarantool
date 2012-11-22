#ifndef TARANTOOL_ASSOC_H_INCLUDED
#define TARANTOOL_ASSOC_H_INCLUDED

/*
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
#include <pickle.h>
#include <string.h>
#include <stdlib.h>

#if !MH_SOURCE
#define MH_UNDEF
#endif

#define MH_TYPEDEFS 1

typedef u32 mh_int_t;

/*
 * Map: (i32) => (void *)
 */
#define mh_name _i32ptr
struct mh_i32ptr_node_t {
	i32 key;
	void *val;
} /* __attribute__((packed)) */;

#define mh_node_t struct mh_i32ptr_node_t
#define mh_hash_arg_t void *
#define mh_hash(a, arg) (a->key)
#define mh_eq_arg_t void *
#define mh_eq(a, b, arg) ((a->key) == (b->key))
#include <mhash.h>


/*
 * Map: (i64) => (void *)
 */
#define mh_name _i64ptr
struct mh_i64ptr_node_t {
	i64 key;
	void *val;
} __attribute__((packed));

#define mh_node_t struct mh_i64ptr_node_t
#define mh_int_t u32
#define mh_hash_arg_t void *
#define mh_hash(a, arg) ((u32)((a->key)>>33^(a->key)^(a->key)<<11))
#define mh_eq_arg_t void *
#define mh_eq(a, b, arg) ((a->key) == (b->key))
#include <mhash.h>

/*
 * Map: (char *) => (void *)
 */
static inline int lstrcmp(void *a, void *b)
{
	unsigned int al, bl;

	al = load_varint32(&a);
	bl = load_varint32(&b);

	if (al != bl)
		return bl - al;
	return memcmp(a, b, al);
}
#include <third_party/murmur_hash2.c>
#define mh_name _lstrptr
struct mh_lstrptr_node_t {
	void *key;
	void *val;
} __attribute__((packed));

#define mh_node_t struct mh_lstrptr_node_t
#define mh_int_t u32
#define mh_hash_arg_t void *
static inline u32
mh_strptr_hash(const mh_node_t *a, mh_hash_arg_t arg) {
	(void) arg;
	void *_k = (a->key);
	const u32 l = load_varint32(&_k);
	return (u32) MurmurHash2(_k, l, 13);
}
#define mh_hash(a, arg) mh_strptr_hash(a, arg)
#define mh_eq_arg_t void *
#define mh_eq(a, b, arg) (lstrcmp(a->key, b->key) == 0)
#include <mhash.h>

#endif /* TARANTOOL_ASSOC_H_INCLUDED */
