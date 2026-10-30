#pragma once

#include <stdio.h>

#define panic(...) do { \
	fprintf(stderr, __VA_ARGS__); \
	abort(); \
} while (false)

#define BUG_ON(cond) do { \
	if (cond) \
		panic("Bug in %s at %s:%d: %s\n", \
		      __func__, __FILE__, __LINE__, #cond); \
} while (false)
