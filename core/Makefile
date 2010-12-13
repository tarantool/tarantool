obj += core/coro.o
obj += core/fiber.o
obj += core/iproto.o
obj += core/log_io.o
obj += core/log_io_remote.o
obj += core/palloc.o
obj += core/pickle.o
obj += core/salloc.o
obj += core/say.o
obj += core/stat.o
obj += core/tarantool_ev.o
obj += core/tbuf.o
obj += core/util.o
obj += core/tarantool.o

core/tarantool.o: tarantool_version.h

core/tarantool_ev.o: CFLAGS += -U_FORTIFY_SOURCE \
			      -Wno-comment \
			      -Wno-extra \
			      -Wno-parentheses \
			      -Wno-unused

ifeq (1,$(RESOLVE_SYMBOLS))
 LDFLAGS += -rdynamic -lbfd
 CFLAGS += -DRESOLVE_SYMBOLS
endif

ifneq (,$(TRACE))
 obj += core/trace.o
 $(TRACE): CFLAGS += -finstrument-functions
 LDFLAGS += -Wl,-Map=tarantool.map
endif
