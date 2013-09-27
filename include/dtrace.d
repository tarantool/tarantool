provider cjson {
	probe new__entry();
	probe encode__start();
	probe encode__done(int len, char *);
};
provider coro {
	probe new__entry();
};
provider ev {
	probe tick__start(int flags);
	probe tick__stop(int flags);
};
