#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Position descriptor. */
struct position {
	/** Size of key in bytes. */
	uint32_t key_size;
	/** Extracted cmp_def of tuple, with array header. */
	const char *key;
};

/** Calculate length of packed position. */
uint32_t
position_pack_size(struct position *pos);

/**
 * Pack position to preallocated buffer. Buffer length must be
 * more or equal than packed size of position.
 */
void
position_pack(struct position *pos, char *buffer);

/**
 * Unpack position from MsgPack. Lifetime of returned position is the same
 * as lifetime of passed ptr. Returns 0 on success, -1 on failure,
 * diag is not set!
 */
int
position_unpack(const char *ptr, struct position *pos);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */
