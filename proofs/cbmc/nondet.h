#pragma once

#include <stdint.h>

/**
 * Non-determinstic functions used in CBMC proofs.
 */
int
nondet_int(void);

uint32_t
nondet_uint32_t(void);
