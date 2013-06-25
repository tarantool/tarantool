#ifndef TS_CURSOR_H_INCLUDED
#define TS_CURSOR_H_INCLUDED

struct ts_cursor {
	struct ts_ref *r;
	struct ts_key *k;
	struct tnt_log current;
};

int
ts_cursor_open(struct ts_cursor *c, struct ts_key *k);

struct tnt_tuple*
ts_cursor_tuple(struct ts_cursor *c);

void
ts_cursor_close(struct ts_cursor *c);

#endif
