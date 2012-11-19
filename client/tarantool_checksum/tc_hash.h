#ifndef TC_HASH_H_INCLUDED
#define TC_HASH_H_INCLUDED

#if !MH_SOURCE
#define MH_UNDEF
#endif

typedef void* ptr_t;

#define mh_name _u32ptr
#define mh_key_t uint32_t
#define mh_val_t ptr_t
#define mh_hash(a) (a)
#define mh_eq(a, b) ((a) == (b))
#include <mhash.h>

uint32_t search_hash(void *x, const struct tc_key *k);
int search_equal(void *x, const struct tc_key *a, const struct tc_key *b);

#undef put_slot

#define mh_name _pk
#define mh_arg_t void*
#define mh_val_t struct tc_key*
#define mh_hash(x, k) search_hash((x), (k))
#define mh_eq(x, ka, kb) search_equal((x), (ka), (kb))
#include "mhash-val.h"

#endif
