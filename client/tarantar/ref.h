#ifndef TS_REF_H_INCLUDED
#define TS_REF_H_INCLUDED

struct ts_ref {
	char *file;
	int is_snap;
};

struct ts_reftable {
	struct ts_ref *r;
	int count;
	int top;
};

int ts_reftable_init(struct ts_reftable *t);
void ts_reftable_free(struct ts_reftable *t);
int ts_reftable_add(struct ts_reftable *t, char *file, int is_snap);
struct ts_ref *ts_reftable_map(struct ts_reftable *t, int id);

#endif
