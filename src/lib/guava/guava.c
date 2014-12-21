#include <stdlib.h>
#include <stdint.h>

#include <lib/guava/guava.h>

static const int64_t K = 2862933555777941757;
static const double  D = 0x1.0p31;

static inline double lcg(int64_t *state) {
	return (double )((int32_t)(((uint64_t )*state >> 33) + 1)) / D;
}

int32_t guava(int64_t state, int32_t buckets) {
	int32_t candidate = 0;
	int32_t next;
	while (1) {
		state = K * state + 1;
		next = (int32_t)((candidate + 1) / lcg(&state));
		if (next >= 0 && next < buckets)
			candidate = next;
		else
			return candidate;
	}
}
