provider tarantool {
	probe new__entry();
	probe tick__start(int flags);
	probe tick__stop(int flags);
	probe encode__start();
	probe encode__done(int len, char *);
};
