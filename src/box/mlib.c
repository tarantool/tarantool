#include "m-dict.h"

static inline bool oor_equal_p(void *k, int n)
{
  return k == (NULL + n);
}

static inline void *oor_set(int n)
{
  return NULL + n;
}

static inline size_t tuple_ptr_hash(void *n)
{
  return (uintptr_t)n >> 3;
}

DICT_OA_DEF2(m_dict_ptrptr, void *, M_OPEXTEND(M_PTR_OPLIST, OOR_EQUAL(oor_equal_p), OOR_SET(API_4(oor_set)), HASH(tuple_ptr_hash)), void *, M_PTR_OPLIST)

void *
ht_ptrptr_new() {
	struct m_dict_ptrptr_s *ht = calloc(sizeof(*ht), 1);
	m_dict_ptrptr_init(ht);
	return ht;
}

void
ht_ptrptr_delete(void *ht)
{
	m_dict_ptrptr_clear(ht);
	free(ht);
}

void
ht_ptrptr_reserve(void *ht, size_t count)
{
	m_dict_ptrptr_reserve(ht, count);
}

void
ht_ptrptr_put(void *ht, void *key, void *val)
{
	m_dict_ptrptr_set_at(ht, key, val);
}

void *
ht_ptrptr_get(void *ht, void *key)
{
	return *m_dict_ptrptr_get(ht, key);
}
