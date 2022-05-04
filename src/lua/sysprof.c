#include "lua/sysprof.h"

#ifdef ENABLE_BACKTRACE

#include "core/fiber.h"
#include "core/backtrace.h"

void
fiber_backtracer(void *(*frame_writer)(int frame_no, void *addr))
{
	struct backtrace bt = {};
	int frame_no = 0;
	backtrace_collect(&bt, fiber_self(), 0);
	for (frame_no = 0; frame_no < bt.frame_count; ++frame_no) {
		frame_writer(frame_no, bt.frames[frame_no].ip);
	}
}

void
tarantool_lua_sysprof_init(void)
{
	luaM_sysprof_set_backtracer(fiber_backtracer);
}

#else

void
tarantool_lua_sysprof_init(void) {};

#endif /* ENABLE_BACKTRACE */
