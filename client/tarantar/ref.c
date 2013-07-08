
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ref.h"

int ts_reftable_init(struct ts_reftable *t)
{
	t->count = 0;
	t->top = 16;
	t->r = malloc(sizeof(struct ts_ref) * t->top);
	if (t == NULL)
		return -1;
	return 0;
}

void ts_reftable_free(struct ts_reftable *t)
{
	free(t->r);
}

int ts_reftable_add(struct ts_reftable *t, char *file, int is_snap)
{
	if (t->count == t->top) {
		t->top *= 2;
		void *p = realloc(t->r, sizeof(struct ts_ref) * t->top);
		if (p == NULL)
			return -1;
		t->r = p;
	}
	int id = t->count;
	t->r[id].file = strdup(file);
	t->r[id].is_snap = is_snap;
	if (t->r[id].file == NULL)
		return -1;
	t->count++;
	return id;
}

struct ts_ref *ts_reftable_map(struct ts_reftable *t, int id)
{
	assert(id < t->count);
	return &t->r[id];
}
