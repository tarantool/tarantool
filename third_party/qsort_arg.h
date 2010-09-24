#ifndef QSORT_ARG_H
#define QSORT_ARG_H

#include <sys/types.h>

void qsort_arg(void *a, size_t n, size_t es, int (*cmp)(const void *a, const void *b, void *arg), void *arg);

#endif
