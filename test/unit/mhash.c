#include <stdint.h>
#include <stdio.h>
#include "unit.h"

#define MH_SOURCE 1

#define mh_name _i32
#define mh_key_t int32_t
#define mh_val_t int32_t
#define mh_hash(a) (a)
#define mh_eq(a, b) ((a) == (b))

#include "mhash.h"

#define mh_name _i32_collision
#define mh_key_t int32_t
#define mh_val_t int32_t
#define mh_hash(a) 42
#define mh_eq(a, b) ((a) == (b))

#include "mhash.h"

#undef MH_SOURCE

static void mhash_int32_id_test()
{
	header();
	int ret, k;
	struct mh_i32_t *h;
#define init()		({ mh_i32_init();		})
#define clear(x)	({ mh_i32_clear((x));		})
#define destroy(x)	({ mh_i32_destroy((x));		})
#define get(x)		({ mh_i32_get(h, (x));		})
#define put(x)		({ mh_i32_put(h, (x), 0, &ret);	})
#define del(x)		({ mh_i32_del(h, (x));		})

#include "mhash_body.c"
	footer();
}


static void mhash_int32_collision_test()
{
	header();
	int ret, k;
	struct mh_i32_collision_t *h;
#define init()		({ mh_i32_collision_init();		})
#define clear(x)	({ mh_i32_collision_clear((x));		})
#define destroy(x)	({ mh_i32_collision_destroy((x));		})
#define get(x)		({ mh_i32_collision_get(h, (x));		})
#define put(x)		({ mh_i32_collision_put(h, (x), 0, &ret);	})
#define del(x)		({ mh_i32_collision_del(h, (x));		})

#include "mhash_body.c"
	footer();
}

int main(void)
{
	mhash_int32_id_test();
	mhash_int32_collision_test();
	return 0;
}
