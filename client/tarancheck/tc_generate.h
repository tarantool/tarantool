#ifndef TC_GENERATE_H_INCLUDED
#define TC_GENERATE_H_INCLUDED

struct tc_key *tc_generate_key(struct tc_space *s, struct tnt_tuple *t);
int tc_generate(struct tc_options *opts);

#endif
