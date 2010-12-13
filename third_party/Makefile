obj += third_party/crc32.o
obj += third_party/daemon.o
obj += third_party/coro/coro.o
obj += third_party/proctitle.o
obj += third_party/confetti/prscfg.o
obj += third_party/gopt/gopt.o

third_party/coro/coro.o third_party/confetti/prscfg.o: CFLAGS += -Wno-unused
third_party/gopt/gopt.o: CFLAGS += -Wno-all

CFLAGS += -DCORO_$(CORO_IMPL)
