#ifndef INCLUDES_TARANTOOL_BOX_H
#define INCLUDES_TARANTOOL_BOX_H
/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <util.h>
/*
 * Box - data storage (spaces, indexes) and query
 * processor (INSERT, UPDATE, DELETE, SELECT, Lua)
 * subsystem of Tarantool/Box.
 */

struct txn;
struct tbuf;
struct port;
struct fio_batch;
struct log_io;
struct tarantool_cfg;
struct lua_State;

/** To be called at program start. */
void box_init(void);
/** To be called at program end. */
void box_free(void);

/**
 * The main entry point to the
 * Box: callbacks into the request processor.
 * These are function pointers since they can
 * change when entering/leaving read-only mode
 * (master->slave propagation).
 */
typedef void (*box_process_func)(struct port *, u32, const void *, u32);
/** For read-write operations. */
extern box_process_func box_process;
/** For read-only port. */
extern box_process_func box_process_ro;

/*
 * Check storage-layer related options in the
 * configuration file.
 */
i32 box_check_config(struct tarantool_cfg *conf);
/*
 * Take into effect storage-layer related
 * changes in the server configuration.
 */
i32 box_reload_config(struct tarantool_cfg *old_conf, struct tarantool_cfg *new_conf);
void box_lua_load_cfg(struct lua_State *L);
/**
 * Ehm, this is a hack, shouldn't be here.
 */
int box_cat(const char *filename);
/**
 * Iterate over all spaces and save them to the
 * snapshot file.
 */
void box_snapshot(struct log_io *, struct fio_batch *batch);
/**
 * Spit out some basic module status (master/slave, etc.
 */
void box_info(struct tbuf *out);
const char *box_status(void);
/**
 * Called to enter master or replica
 * mode (depending on the configuration) after
 * binding to the primary port.
 */
void
box_leave_local_standby_mode(void *data __attribute__((unused)));

enum {
	BOX_SPACE_MAX = UINT32_MAX,
	BOX_INDEX_MAX = 10,
	BOX_FIELD_MAX = UINT32_MAX
};

#endif /* INCLUDES_TARANTOOL_BOX_H */
