#ifndef TC_SPACE_H_INCLUDED
#define TC_SPACE_H_INCLUDED

enum tc_space_key_type {
	TC_SPACE_KEY_UNKNOWN = -1,
	TC_SPACE_KEY_NUM = 0,
	TC_SPACE_KEY_NUM64,
	TC_SPACE_KEY_STRING
};

struct tc_space_key_field {
	enum tc_space_key_type type;
	int n;
};

struct tc_space_key {
	struct tc_space_key_field *fields;
	int count;
};

struct tc_space {
	uint32_t id;
	struct mh_pk_t *hash_log;
	struct mh_pk_t *hash_snap;
	struct tc_space_key pk;
};

struct tc_spaces {
	struct mh_u32ptr_t *t;
};

int tc_space_init(struct tc_spaces *s);
void tc_space_free(struct tc_spaces *s);

struct tc_space *tc_space_create(struct tc_spaces *s, uint32_t id);
struct tc_space *tc_space_match(struct tc_spaces *s, uint32_t id);

#if 0
int tc_space_fill(struct tc_spaces *s, struct tc_options *opts);
#endif
#endif
