#ifndef QSORT_ARG_H
#define QSORT_ARG_H

#include <trivia/config.h>
#include <sys/types.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * General version of qsort that calls single-threaded of multi-threaded
 * qsort depending on open MP availability and given array size.
 */
void qsort_arg(void *a, size_t n, size_t es,
	       int (*cmp)(const void *a, const void *b, void *arg), void *arg);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif
