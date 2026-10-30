#define set(x) ({							\
	k = put(x);							\
	val(k) = (x) << 1;						\
})


#define rm(x) ({							\
	mh_int_t k = get(x);						\
	del(k);								\
})

#define tst(x) ({							\
	mh_int_t k = get((x));						\
	fail_unless(k != mh_end(h));					\
	fail_unless(val(k) == ((x) << 1));				\
})


#define clr(x) fail_unless(get(x) == mh_end(h))
#define usd(x) fail_unless(get(x) != mh_end(h))

h = init();
destroy(h);

h = init();
clear(h);

/* access not yet initialized hash */
clr(9);

/* set & test some data. there is first resize here */
set(1);
set(2);
set(3);

tst(1);
tst(2);
tst(3);

/* delete non existing entry; note: index must come from get */
set(4);
k = get(4);
del(k);
del(k);
del(get(4));

set(4);
set(5);
set(6);
set(7);
set(8);
set(9);

/* there is resize after 8 elems. verify they are inplace */
tst(4);
tst(5);
tst(6);
tst(7);
tst(8);
tst(9);

clear(h);

/* after clear no items should exist */
clr(1);
clr(2);
clr(3);
clr(4);
clr(5);
clr(6);
clr(7);
clr(8);
clr(9);
clr(10);
clr(11);

/* set after del */
set(1);
rm(1);
set(1);

destroy(h);
h = init();
set(0);
set(1);
set(2);
set(3);
set(4);
set(5);
set(6);
set(7);

usd(0);
rm(0);
clr(0);
usd(1);
rm(1);
clr(1);
usd(2);
rm(2);
clr(2);
usd(3);
rm(3);
clr(3);
usd(4);
rm(4);
clr(4);
usd(5);
rm(5);
clr(5);
usd(6);
rm(6);
clr(6);
usd(7);
rm(7);
clr(7);

set(8);
set(9);
set(10);
tst(8);
tst(9);
tst(10);

set(1);
set(1);
tst(1);

rm(1);
rm(1);
clr(1);

/* verify overflow of hash index over hash table */
int i;
for (i = 0 ; i < 20; i++) {
	set(i);
}
for (i = 0 ; i < 20; i++) {
	tst(i);
}

destroy(h);

h = init();
set(0);
set(1);
set(2);
set(3);
set(4);
set(5);
set(6);
set(7);
rm(0);
rm(1);
rm(2);
rm(3);
rm(4);

destroy(h);

/* verify reuse of deleted elements */
h = init();
set(1);
int k1 = get(1);
rm(1);
set(1);
int k2 = get(1);
fail_unless(k1 == k2);
destroy(h);

#undef set
#undef rm
#undef tst
#undef clr
#undef usd

#undef init
#undef clear
#undef destroy
#undef get
#undef put
#undef del
#undef key
#undef val
