#include <limits.h>

/*
 * The definition of function set_sigint_cb is needed to avoid the error
 * while linking. This occurs because libbox.a contains the unresolved
 * lbox_console_readline() symbol, and it is necessary to construct a unit
 * test executable.
 */
typedef struct ev_loop ev_loop;
struct ev_signal;
typedef void
(*sigint_cb_t)(ev_loop *loop, struct ev_signal *w, int revents);

sigint_cb_t
set_sigint_cb(sigint_cb_t new_sigint_cb)
{
	(void)new_sigint_cb;
	return new_sigint_cb;
}

char tarantool_path[PATH_MAX];
long tarantool_start_time;
