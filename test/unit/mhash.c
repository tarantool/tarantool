#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "unit.h"

#ifndef bytemap
#define bytemap 0
#endif
#define MH_SOURCE 1


#define mh_name _i32
struct mh_i32_node_t {
	int32_t key;
	int32_t val;
};
#define mh_node_t struct mh_i32_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) (a->key)
#define mh_cmp(a, b, arg) ((a->key) != (b->key))
#define mh_bytemap bytemap
#include "salad/mhash.h"

#define mh_name _i32_collision
struct mh_i32_collision_node_t {
	int32_t key;
	int32_t val;
};
#define mh_node_t struct mh_i32_collision_node_t
#define mh_arg_t void *
#define mh_hash(a, arg) 42
#define mh_cmp(a, b, arg) ((a->key) != (b->key))

#define mh_bytemap bytemap
#include "salad/mhash.h"

#undef MH_SOURCE

static void mhash_int32_id_test()
{
	header();
	plan(0);
	int k;
	struct mh_i32_t *h;
#define init()		({ mh_i32_new();		})
#define clear(x)	({ mh_i32_clear((x));		})
#define destroy(x)	({ mh_i32_delete((x));		})
#define get(x) ({							\
	const struct mh_i32_node_t _node = { .key = (x) };		\
	mh_i32_get(h, &_node, NULL);					\
})
#define put(x) ({							\
	const struct mh_i32_node_t _node = { .key = (x) };		\
	mh_i32_put(h, &_node, NULL, NULL);				\
})
#define key(k) (mh_i32_node(h, k)->key)
#define val(k) (mh_i32_node(h, k)->val)
#define del(k) ({							\
	mh_i32_del(h, k, NULL);						\
})

#include "mhash_body.c"
	footer();
	check_plan();
}


static void mhash_int32_collision_test()
{
	header();
	plan(0);
	int k;
	struct mh_i32_collision_t *h;
#define init()		({ mh_i32_collision_new();		})
#define clear(x)	({ mh_i32_collision_clear((x));	})
#define destroy(x)	({ mh_i32_collision_delete((x));	})
#define get(x) ({							\
	const struct mh_i32_collision_node_t _node = { .key = (x) };	\
	mh_i32_collision_get(h, &_node, NULL);				\
})
#define put(x) ({							\
	const struct mh_i32_collision_node_t _node = { .key = (x) };	\
	mh_i32_collision_put(h, &_node, NULL, NULL);			\
})
#define key(k) (mh_i32_collision_node(h, k)->key)
#define val(k) (mh_i32_collision_node(h, k)->val)
#define del(k) ({							\
	mh_i32_collision_del(h, k, NULL);				\
})

#include "mhash_body.c"
	footer();
	check_plan();
}

static void
mhash_random_test(void)
{
	header();
	plan(3);
	struct mh_i32_t *h = mh_i32_new();
	const int end = 100;
	int i, size;
	bool is_found[end], all_is_found[end];
	memset(all_is_found, 1, sizeof(all_is_found));

	for (i = 0; i < end; ++i) {
		if (mh_i32_random(h, i) != mh_end(h))
			break;
	}
	is(i, end, "empty random is always 'end'");

	for (i = 0; i < end; ++i) {
		struct mh_i32_node_t node = {i, i};
		mh_int_t rc = mh_i32_put(h, &node, NULL, NULL);
		int j;
		for (j = 0; j < end; ++j) {
			if (mh_i32_random(h, j) != rc)
				break;
		}
		mh_i32_del(h, rc, NULL);
		if (j != end)
			break;
	}
	is(i, end, "one element is always found");

	for (i = 0, size = sizeof(bool); i < end; ++i, size += sizeof(bool)) {
		struct mh_i32_node_t *n, node = {i, i};
		mh_i32_put(h, &node, NULL, NULL);
		memset(is_found, 0, sizeof(is_found));
		for (int j = 0; j < end; ++j) {
			mh_int_t rc = mh_i32_random(h, j);
			n = mh_i32_node(h, rc);
			is_found[n->key] = true;
		}
		if (memcmp(is_found, all_is_found, size) != 0)
			break;
	}
	is(i, end, "incremental random from mutable hash");

	mh_i32_delete(h);
	check_plan();
	footer();
}

int main(void)
{
	header();
	plan(3);

	mhash_int32_id_test();
	mhash_int32_collision_test();
	mhash_random_test();

	int rc = check_plan();
	footer();
	return rc;
}
