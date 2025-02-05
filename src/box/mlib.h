#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

void *
ht_ptrptr_new();

void
ht_ptrptr_delete(void *ht);

void
ht_ptrptr_reserve(void *ht, size_t count);

void
ht_ptrptr_put(void *ht, void *key, void *val);

void *
ht_ptrptr_get(void *ht, void *key);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
