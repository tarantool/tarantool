#ifndef TS_SPACE_H_INCLUDED
#define TS_SPACE_H_INCLUDED

enum ts_space_key_type {
	TS_SPACE_KEY_UNKNOWN = -1,
	TS_SPACE_KEY_NUM = 0,
	TS_SPACE_KEY_NUM64,
	TS_SPACE_KEY_STRING
};

enum ts_space_compact {
	TS_SPACE_COMPACT_CHECKSUM,
	TS_SPACE_COMPACT_SPARSE
};

struct ts_space_key_field {
	enum ts_space_key_type type;
	int n;
};

struct ts_space_key {
	struct ts_space_key_field *fields;
	int count;
};

struct ts_space {
	enum ts_space_compact c;
	int key_size;
	int key_div;

	uint32_t id;
	struct mh_pk_t *index;
	struct ts_space_key pk;
};

struct ts_spaces {
	struct mh_u32ptr_t *t;
};

int ts_space_init(struct ts_spaces *s);
void ts_space_free(struct ts_spaces *s);

struct ts_space *ts_space_create(struct ts_spaces *s, uint32_t id);
struct ts_space *ts_space_match(struct ts_spaces *s, uint32_t id);

int ts_space_fill(struct ts_spaces *s, struct ts_options *opts);

struct ts_key*
ts_space_keyalloc(struct ts_space *s, struct tnt_tuple *t, int fileid,
                  int offset, int attach);

#endif
