#include "key_def.h"

/**
 * Sets a hash functions for the key_def
 *
 * @param key_def key_definition
 *
 */
void
tuple_hash_func_set(struct key_def *def);

/**
 * Calculates a common hash value for a tuple
 * @param tuple - a tuple
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
tuple_hash(const struct tuple *tuple, const struct key_def *key_def)
{
	return key_def->tuple_hash(tuple, key_def);
}

/**
 * Calculate a common hash value for a key
 * @param key - full key (msgpack fields w/o array marker)
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
key_hash(const char *key, const struct key_def *key_def)
{
	return key_def->key_hash(key, key_def);
}
