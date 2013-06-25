#ifndef TC_HASH_H_INCLUDED
#define TC_HASH_H_INCLUDED

#if !MH_SOURCE
#define MH_UNDEF
#endif

#include <stdint.h>

#define mh_name _u32ptr
struct mh_u32ptr_node_t {
	uint32_t key;
	void *val;
};

#define mh_node_t struct mh_u32ptr_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) (a->key)
#define mh_eq(a, b, arg) ((a->key) == (b->key))
#include <mhash.h>


struct ts_space;

uint32_t
search_hash(const struct ts_key *k, struct ts_space *s);

int
search_equal(const struct ts_key *a, const struct ts_key *b,
	     struct ts_space *s);

#define mh_name _pk
#define mh_node_t struct ts_key *
#define mh_arg_t struct ts_space *
#define mh_hash(a, arg) search_hash(*(a), arg)
#define mh_eq(a, b, arg) search_equal(*(a), *(b), arg)
#include <mhash.h>

#endif
