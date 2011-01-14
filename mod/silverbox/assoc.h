#ifndef BOX_ASSOC_H
#define BOX_ASSOC_H
/* associative array */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <third_party/khash.h>
#include <say.h>
#include <pickle.h>

typedef void *ptr_t;

KHASH_MAP_INIT_INT(int_ptr_map, ptr_t, realloc);
KHASH_MAP_INIT_INT64(int64_ptr_map, ptr_t, realloc);
KHASH_MAP_INIT_STR(str_ptr_map, ptr_t, realloc);
KHASH_MAP_INIT_INT(int_int_map, uint32_t, realloc);
KHASH_MAP_INIT_INT(seen, int32_t, realloc);
KHASH_SET_INIT_INT(seen_set, realloc);
KHASH_SET_INIT_INT(int_set, realloc);

static inline khint_t __ac_X31_hash_lstr(void *s)
{
	khint_t l;
	l = load_varint32(&s);
	khint_t h = 0;
	if (l)
		for (; l--; s++)
			h = (h << 5) - h + *(u8 *)s;
	return h;
}

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
#define kh_lstr_hash_func(key) ({ void *_k = key; unsigned int l = load_varint32(&_k); MurmurHash2(_k, l, 13); })
#define kh_lstr_hash_equal(a, b) (lstrcmp(a, b) == 0)

KHASH_INIT(lstr_ptr_map, void *, ptr_t, 1, kh_lstr_hash_func, kh_lstr_hash_equal, xrealloc);
KHASH_INIT(ptr_set, uint64_t, char, 0, kh_int64_hash_func, kh_int64_hash_equal, xrealloc);

void assoc_init(void);

#define assoc_clear(hash_name, hash) kh_clear(hash_name, hash)

#define assoc_exist(hash_name, hash, key) ({       \
        khint_t _k = kh_get(hash_name, hash, key); \
        bool _ret = 0;                             \
                                                   \
        if (_k != kh_end(hash))                    \
                _ret = kh_exist(hash, _k);         \
        _ret;                                      \
})

#define assoc_find(hash_name, hash, key, val) ({   \
        khint_t _k = kh_get(hash_name, hash, key); \
        bool _ret = 0;                             \
                                                   \
        if (_k != kh_end(hash)) {                  \
                val = kh_value(hash, _k);          \
                _ret = 1;                          \
        }                                          \
        _ret;                                      \
})

#define assoc_insert(hash_name, hash, key, val) assoc_replace(hash_name, hash, key, val);

#define assoc_delete(hash_name, hash, key) ({                           \
                        khiter_t _k = kh_get(hash_name, hash, key);     \
                        kh_del(hash_name, hash, _k);                    \
                })

#define assoc_replace(hash_name, hash, key, val) ({ \
        int _ret;                                   \
        khiter_t _k;                                \
                                                    \
        _k = kh_put(hash_name, hash, key, &_ret);   \
        kh_key(hash, _k) = key;                     \
        kh_value(hash, _k) = val;                   \
                                                    \
        _k;                                         \
})

#define assoc_foreach(hash, kiter)                                    \
        for (kiter = kh_begin(hash); kiter != kh_end(hash); ++kiter)  \
                if (kh_exist(hash, kiter))

#endif
