/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "box/lua/init.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lib/core/mp_extension_types.h"

#include "lua/utils.h" /* luaT_error() */
#include "lua/trigger.h"
#include "lua/msgpack.h"
#include "lua/builtin_modcache.h"

#include "box/box.h"
#include "box/txn.h"
#include "box/func.h"
#include "box/mp_error.h"

#include "box/lua/error.h"
#include "box/lua/tuple.h"
#include "box/lua/call.h"
#include "box/lua/slab.h"
#include "box/lua/index.h"
#include "box/lua/space.h"
#include "box/lua/sequence.h"
#include "box/lua/misc.h"
#include "box/lua/stat.h"
#include "box/lua/info.h"
#include "box/lua/ctl.h"
#include "box/lua/session.h"
#include "box/lua/net_box.h"
#include "box/lua/cfg.h"
#include "box/lua/xlog.h"
#include "box/lua/console.h"
#include "box/lua/lib.h"
#include "box/lua/tuple.h"
#include "box/lua/execute.h"
#include "box/lua/key_def.h"
#include "box/lua/merger.h"
#include "box/lua/watcher.h"
#include "box/lua/iproto.h"
#include "box/lua/audit.h"
#include "box/lua/flight_recorder.h"
#include "box/lua/read_view.h"
#include "box/lua/security.h"
#include "box/lua/space_upgrade.h"
#include "box/lua/wal_ext.h"

#include "mpstream/mpstream.h"

static uint32_t CTID_STRUCT_TXN_SAVEPOINT_PTR = 0;

extern char session_lua[],
	tuple_lua[],
	key_def_lua[],
	schema_lua[],
	load_cfg_lua[],
	xlog_lua[],
#if ENABLE_FEEDBACK_DAEMON
	feedback_daemon_lua[],
#endif
	net_box_lua[],
	upgrade_lua[],
	console_lua[],
	merger_lua[],
	checks_version_lua[],
	checks_lua[],
	metrics_api_lua[],
	metrics_cartridge_failover_lua[],
	metrics_cartridge_issues_lua[],
	metrics_cfg_lua[],
	metrics_collectors_shared_lua[],
	metrics_collectors_counter_lua[],
	metrics_collectors_gauge_lua[],
	metrics_collectors_histogram_lua[],
	metrics_collectors_summary_lua[],
	metrics_const_lua[],
	metrics_http_middleware_lua[],
	metrics_lua[],
	metrics_plugins_graphite_lua[],
	metrics_plugins_prometheus_lua[],
	metrics_plugins_json_lua[],
	metrics_psutils_cpu_lua[],
	metrics_psutils_psutils_linux_lua[],
	metrics_quantile_lua[],
	metrics_registry_lua[],
	metrics_stash_lua[],
	metrics_tarantool_clock_lua[],
	metrics_tarantool_cpu_lua[],
	metrics_tarantool_event_loop_lua[],
	metrics_tarantool_fibers_lua[],
	metrics_tarantool_info_lua[],
	metrics_tarantool_luajit_lua[],
	metrics_tarantool_memory_lua[],
	metrics_tarantool_memtx_lua[],
	metrics_tarantool_network_lua[],
	metrics_tarantool_operations_lua[],
	metrics_tarantool_replicas_lua[],
	metrics_tarantool_runtime_lua[],
	metrics_tarantool_slab_lua[],
	metrics_tarantool_spaces_lua[],
	metrics_tarantool_system_lua[],
	metrics_tarantool_vinyl_lua[],
	metrics_tarantool_lua[],
	metrics_utils_lua[],
	metrics_version_lua[],
	/* {{{ config */
	config_applier_box_cfg_lua[],
	config_applier_mkdir_lua[],
	config_cluster_config_lua[],
	config_configdata_lua[],
	config_instance_config_lua[],
	config_utils_log_lua[],
	config_utils_schema_lua[];
	/* }}} config */

/**
 * List of box's built-in modules written using Lua.
 *
 * Each module is defined as a triplet:
 *
 * 1. A file name (without the .lua extension).
 *
 *    It is used for prepending error messages and filling
 *    debug.getinfo() information.
 * 2. A module name for require().
 *
 *    NULL means 'do not register as a module, just execute'.
 *    Such code shouldn't return any value.
 *
 *    Typical NULL usage: code that define functions in
 *    box or box.internal.
 * 3. A Lua source code of the module.
 */
static const char *lua_sources[] = {
	"box/session", NULL, session_lua,
	"box/tuple", NULL, tuple_lua,
	"box/schema", NULL, schema_lua,
#if ENABLE_FEEDBACK_DAEMON
	/*
	 * It is important to initialize the daemon before
	 * load_cfg, because the latter picks up some values
	 * from the feedback daemon.
	 */
	"box/feedback_daemon", NULL, feedback_daemon_lua,
#endif
	/*
	 * Must be loaded after schema_lua, because it redefines
	 * box.schema.space.upgrade.
	 */
	SPACE_UPGRADE_BOX_LUA_MODULES
	AUDIT_BOX_LUA_MODULES
	FLIGHT_RECORDER_BOX_LUA_MODULES
	READ_VIEW_BOX_LUA_MODULES
	SECURITY_BOX_LUA_MODULES
	"box/xlog", "xlog", xlog_lua,
	"box/upgrade", NULL, upgrade_lua,
	"box/net_box", "net.box", net_box_lua,
	"box/console", "console", console_lua,
	"box/load_cfg", NULL, load_cfg_lua,
	"box/key_def", "key_def", key_def_lua,
	"box/merger", "merger", merger_lua,
	/*
	 * To support tarantool-only types with checks, the module
	 * must be loaded after decimal and datetime lua modules
	 * and after box.tuple and box.error box modules. (Beware
	 * that it won't fail to load if modules not found since
	 * checks supports pure luajit and older tarantool versions).
	 * Module components order is important here.
	 */
	"third_party/checks/checks/version",
	"checks.version", checks_version_lua,
	"third_party/checks/checks", "checks", checks_lua,
	/*
	 * Metrics uses checks. Module components order is also important here
	 * (see https://github.com/tarantool/metrics/issues/433
	 * and https://github.com/tarantool/metrics/pull/434).
	 */
	"third_party/metrics/metrics/const",
	"metrics.const", metrics_const_lua,
	"third_party/metrics/metrics/registry",
	"metrics.registry", metrics_registry_lua,
	"third_party/metrics/metrics/quantile",
	"metrics.quantile", metrics_quantile_lua,
	"third_party/metrics/metrics/stash",
	"metrics.stash", metrics_stash_lua,
	"third_party/metrics/metrics/collectors/shared",
	"metrics.collectors.shared", metrics_collectors_shared_lua,
	"third_party/metrics/metrics/collectors/counter",
	"metrics.collectors.counter", metrics_collectors_counter_lua,
	"third_party/metrics/metrics/collectors/gauge",
	"metrics.collectors.gauge", metrics_collectors_gauge_lua,
	"third_party/metrics/metrics/collectors/histogram",
	"metrics.collectors.histogram", metrics_collectors_histogram_lua,
	"third_party/metrics/metrics/collectors/summary",
	"metrics.collectors.summary", metrics_collectors_summary_lua,
	"third_party/metrics/metrics/api", "metrics.api", metrics_api_lua,
	"third_party/metrics/metrics/utils",
	"metrics.utils", metrics_utils_lua,
	"third_party/metrics/metrics/http_middleware",
	"metrics.http_middleware", metrics_http_middleware_lua,
	"third_party/metrics/metrics/cartridge/failover",
	"metrics.cartridge.failover", metrics_cartridge_failover_lua,
	"third_party/metrics/metrics/cartridge/issues",
	"metrics.cartridge.issues", metrics_cartridge_issues_lua,
	"third_party/metrics/metrics/psutils/psutils_linux",
	"metrics.psutils.psutils_linux", metrics_psutils_psutils_linux_lua,
	"third_party/metrics/metrics/psutils/cpu",
	"metrics.psutils.cpu", metrics_psutils_cpu_lua,
	"third_party/metrics/metrics/tarantool/clock",
	"metrics.tarantool.clock", metrics_tarantool_clock_lua,
	"third_party/metrics/metrics/tarantool/cpu",
	"metrics.tarantool.cpu", metrics_tarantool_cpu_lua,
	"third_party/metrics/metrics/tarantool/event_loop",
	"metrics.tarantool.event_loop", metrics_tarantool_event_loop_lua,
	"third_party/metrics/metrics/tarantool/fibers",
	"metrics.tarantool.fibers", metrics_tarantool_fibers_lua,
	"third_party/metrics/metrics/tarantool/info",
	"metrics.tarantool.info", metrics_tarantool_info_lua,
	"third_party/metrics/metrics/tarantool/luajit",
	"metrics.tarantool.luajit", metrics_tarantool_luajit_lua,
	"third_party/metrics/metrics/tarantool/memory",
	"metrics.tarantool.memory", metrics_tarantool_memory_lua,
	"third_party/metrics/metrics/tarantool/memtx",
	"metrics.tarantool.memtx", metrics_tarantool_memtx_lua,
	"third_party/metrics/metrics/tarantool/network",
	"metrics.tarantool.network", metrics_tarantool_network_lua,
	"third_party/metrics/metrics/tarantool/operations",
	"metrics.tarantool.operations", metrics_tarantool_operations_lua,
	"third_party/metrics/metrics/tarantool/replicas",
	"metrics.tarantool.replicas", metrics_tarantool_replicas_lua,
	"third_party/metrics/metrics/tarantool/runtime",
	"metrics.tarantool.runtime", metrics_tarantool_runtime_lua,
	"third_party/metrics/metrics/tarantool/slab",
	"metrics.tarantool.slab", metrics_tarantool_slab_lua,
	"third_party/metrics/metrics/tarantool/spaces",
	"metrics.tarantool.spaces", metrics_tarantool_spaces_lua,
	"third_party/metrics/metrics/tarantool/system",
	"metrics.tarantool.system", metrics_tarantool_system_lua,
	"third_party/metrics/metrics/tarantool/vinyl",
	"metrics.tarantool.vinyl", metrics_tarantool_vinyl_lua,
	"third_party/metrics/metrics/tarantool",
	"metrics.tarantool", metrics_tarantool_lua,
	"third_party/metrics/metrics/version",
	"metrics.version", metrics_version_lua,
	"third_party/metrics/metrics/cfg", "metrics.cfg", metrics_cfg_lua,
	"third_party/metrics/metrics/init", "metrics", metrics_lua,
	"third_party/metrics/metrics/plugins/graphite",
	"metrics.plugins.graphite", metrics_plugins_graphite_lua,
	"third_party/metrics/metrics/plugins/prometheus",
	"metrics.plugins.prometheus", metrics_plugins_prometheus_lua,
	"third_party/metrics/metrics/plugins/json",
	"metrics.plugins.json", metrics_plugins_json_lua,

	/* {{{ config */

	/*
	 * The order is important: we should load base modules
	 * first and then load ones that use them. Otherwise the
	 * require() call fails.
	 *
	 * General speaking the order here is the following:
	 *
	 * - utility functions
	 * - parts of the general logic
	 * - configuration sources
	 * - configuration appliers
	 * - the entrypoint
	 */

	"config/utils/log",
	"internal.config.utils.log",
	config_utils_log_lua,

	"config/utils/schema",
	"internal.config.utils.schema",
	config_utils_schema_lua,

	"config/instance_config",
	"internal.config.instance_config",
	config_instance_config_lua,

	"config/cluster_config",
	"internal.config.cluster_config",
	config_cluster_config_lua,

	"config/configdata",
	"internal.config.configdata",
	config_configdata_lua,

	"config/applier/box_cfg",
	"internal.config.applier.box_cfg",
	config_applier_box_cfg_lua,

	"config/applier/mkdir",
	"internal.config.applier.mkdir",
	config_applier_mkdir_lua,

	/* }}} config */

	NULL
};

static int
lbox_commit(lua_State *L)
{
	if (box_txn_commit() != 0)
		return luaT_error(L);
	return 0;
}

static int
lbox_rollback(lua_State *L)
{
	(void)L;
	if (box_txn_rollback() != 0)
		return luaT_error(L);
	return 0;
}

/**
 * Extract <struct txn_savepoint *> from a cdata value on the Lua
 * stack.
 *
 * The function is a helper for extracting 'csavepoint' field from
 * a Lua table created using box.savepoint().
 */
static struct txn_savepoint *
luaT_check_txn_savepoint_cdata(struct lua_State *L, int idx)
{
	if (lua_type(L, idx) != LUA_TCDATA)
		return NULL;

	uint32_t cdata_type;
	struct txn_savepoint **svp_ptr = luaL_checkcdata(L, idx, &cdata_type);
	if (svp_ptr == NULL || cdata_type != CTID_STRUCT_TXN_SAVEPOINT_PTR)
		return NULL;
	return *svp_ptr;
}

/**
 * Extract a savepoint from the Lua stack.
 *
 * Expected a value that is created using box.savepoint():
 *
 * {
 *     csavepoint = <cdata<struct txn_savepoint *>>,
 *     txn_id = <cdata<int64_t>>,
 * }
 */
static struct txn_savepoint *
luaT_check_txn_savepoint(struct lua_State *L, int idx, int64_t *svp_txn_id_ptr)
{
	/* Verify passed value type. */
	if (lua_type(L, idx) != LUA_TTABLE)
		return NULL;

	/* Extract and verify csavepoint. */
	lua_getfield(L, idx, "csavepoint");
	struct txn_savepoint *svp = luaT_check_txn_savepoint_cdata(L, -1);
	lua_pop(L, 1);
	if (svp == NULL)
		return NULL;

	/* Extract and verify transaction id from savepoint. */
	lua_getfield(L, idx, "txn_id");
	int64_t svp_txn_id = luaL_toint64(L, -1);
	lua_pop(L, 1);
	if (svp_txn_id == 0)
		return NULL;
	*svp_txn_id_ptr = svp_txn_id;

	return svp;
}

/**
 * Rollback to a savepoint.
 *
 * At success push nothing to the Lua stack.
 *
 * At any error raise a Lua error.
 */
static int
lbox_rollback_to_savepoint(struct lua_State *L)
{
	int64_t svp_txn_id;
	struct txn_savepoint *svp;

	if (lua_gettop(L) != 1 ||
	    (svp = luaT_check_txn_savepoint(L, 1, &svp_txn_id)) == NULL)
		return luaL_error(L,
			"Usage: box.rollback_to_savepoint(savepoint)");

	/*
	 * Verify that we're in a transaction and that it is the
	 * same transaction as one where the savepoint was
	 * created.
	 */
	struct txn *txn = in_txn();
	if (txn == NULL || svp_txn_id != txn->id) {
		diag_set(ClientError, ER_NO_SUCH_SAVEPOINT);
		return luaT_error(L);
	}

	/*
	 * All checks have been passed: try to rollback to the
	 * savepoint.
	 */
	int rc = box_txn_rollback_to_savepoint(svp);
	if (rc != 0)
		return luaT_error(L);

	return 0;
}

/**
 * Get a next txn statement from the current transaction. This is
 * a C closure and 2 upvalues should be available: first is a
 * transaction id, second is a previous statement. This function
 * works only inside on commit trigger of the concrete
 * transaction.
 * It takes two parameters according to Lua 'for' semantics: the
 * first is iterator (that here is nil and unused), the second is
 * key of iteration - here it is integer growing from 1 to
 * txn->n_rows.
 * It returns values with respect to Lua 'for' as well: the first
 * is the next key (previous + 1), the 2th - 4th are a statement
 * attributes: old tuple or nil, new tuple or nil, space id.
 */
static int
lbox_txn_iterator_next(struct lua_State *L)
{
	struct txn *txn = in_txn();
	int64_t txn_id = luaL_toint64(L, lua_upvalueindex(1));
	if (txn == NULL || txn->id != txn_id) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
		return luaT_error(L);
	}
	struct txn_stmt *stmt =
		(struct txn_stmt *) lua_topointer(L, lua_upvalueindex(2));
	if (stmt == NULL)
		return 0;
	while (stmt->row == NULL) {
		stmt = stailq_next_entry(stmt, next);
		if (stmt == NULL) {
			lua_pushnil(L);
			lua_replace(L, lua_upvalueindex(2));
			return 0;
		}
	}
	lua_pushinteger(L, lua_tointeger(L, 2) + 1);
	if (stmt->old_tuple != NULL)
		luaT_pushtuple(L, stmt->old_tuple);
	else
		lua_pushnil(L);
	if (stmt->new_tuple != NULL)
		luaT_pushtuple(L, stmt->new_tuple);
	else
		lua_pushnil(L);
	lua_pushinteger(L, space_id(stmt->space));
	/* Prepare a statement to the next call. */
	stmt = stailq_next_entry(stmt, next);
	lua_pushlightuserdata(L, stmt);
	lua_replace(L, lua_upvalueindex(2));
	return 4;
}

/**
 * Open an iterator over the transaction statements. This is a C
 * closure and 1 upvalue should be available - id of the
 * transaction to iterate over.
 * It returns 3 values which can be used in Lua 'for': iterator
 * generator function, unused nil and the zero key.
 */
static int
lbox_txn_pairs(struct lua_State *L)
{
	int64_t txn_id = luaL_toint64(L, lua_upvalueindex(1));
	struct txn *txn = in_txn();
	if (txn == NULL || txn->id != txn_id) {
		diag_set(ClientError, ER_CURSOR_NO_TRANSACTION);
		return luaT_error(L);
	}
	luaL_pushint64(L, txn_id);
	lua_pushlightuserdata(L, stailq_first_entry(&txn->stmts,
						    struct txn_stmt, next));
	lua_pushcclosure(L, lbox_txn_iterator_next, 2);
	lua_pushnil(L);
	lua_pushinteger(L, 0);
	return 3;
}

/**
 * Push an argument for on_commit Lua trigger. The argument is
 * a function to open an iterator over the transaction statements.
 */
static int
lbox_push_txn(struct lua_State *L, void *event)
{
	struct txn *txn = (struct txn *) event;
	luaL_pushint64(L, txn->id);
	lua_pushcclosure(L, lbox_txn_pairs, 1);
	return 1;
}

/**
 * Update the transaction on_commit/rollback triggers.
 * @sa lbox_trigger_reset.
 */
#define LBOX_TXN_TRIGGER(name)                                                 \
static int                                                                     \
lbox_on_##name(struct lua_State *L) {                                          \
	struct txn *txn = in_txn();                                            \
	int top = lua_gettop(L);                                               \
	if (top > 2 || txn == NULL) {                                          \
		return luaL_error(L, "Usage inside a transaction: "            \
				  "box.on_" #name "([function | nil, "         \
				  "[function | nil]])");                       \
	}                                                                      \
	txn_init_triggers(txn);                                                \
	return lbox_trigger_reset(L, 2, &txn->on_##name, lbox_push_txn, NULL); \
}

LBOX_TXN_TRIGGER(commit)
LBOX_TXN_TRIGGER(rollback)

static int
lbox_snapshot(struct lua_State *L)
{
	int ret = box_checkpoint();
	if (ret == 0) {
		lua_pushstring(L, "ok");
		return 1;
	}
	return luaT_error(L);
}

/** Argument passed to lbox_backup_fn(). */
struct lbox_backup_arg {
	/** Lua state. */
	struct lua_State *L;
	/** Number of files in the resulting table. */
	int file_count;
};

static int
lbox_backup_cb(const char *path, void *cb_arg)
{
	struct lbox_backup_arg *arg = cb_arg;
	lua_pushinteger(arg->L, ++arg->file_count);
	lua_pushstring(arg->L, path);
	lua_settable(arg->L, -3);
	return 0;
}

static int
lbox_backup_start(struct lua_State *L)
{
	int checkpoint_idx = 0;
	if (lua_gettop(L) > 0) {
		checkpoint_idx = luaL_checkint(L, 1);
		if (checkpoint_idx < 0)
			return luaL_error(L, "invalid checkpoint index");
	}
	lua_newtable(L);
	struct lbox_backup_arg arg = {
		.L = L,
	};
	if (box_backup_start(checkpoint_idx, lbox_backup_cb, &arg) != 0)
		return luaT_error(L);
	return 1;
}

static int
lbox_backup_stop(struct lua_State *L)
{
	(void)L;
	box_backup_stop();
	return 0;
}

static const struct luaL_Reg boxlib[] = {
	{"commit", lbox_commit},
	{"rollback", lbox_rollback},
	{"on_commit", lbox_on_commit},
	{"on_rollback", lbox_on_rollback},
	{"snapshot", lbox_snapshot},
	{"rollback_to_savepoint", lbox_rollback_to_savepoint},
	{NULL, NULL}
};

static const struct luaL_Reg boxlib_backup[] = {
	{"start", lbox_backup_start},
	{"stop", lbox_backup_stop},
	{NULL, NULL}
};

/**
 * A MsgPack extensions handler, for types defined in box.
 */
static enum mp_type
luamp_encode_extension_box(struct lua_State *L, int idx,
			   struct mpstream *stream)
{
	struct tuple *tuple = luaT_istuple(L, idx);
	if (tuple != NULL) {
		tuple_to_mpstream(tuple, stream);
		return MP_ARRAY;
	}
	struct error *err = luaL_iserror(L, idx);
	if (err != NULL)
		error_to_mpstream(err, stream);

	return MP_EXT;
}

/**
 * A MsgPack extensions handler that supports errors decode.
 */
static void
luamp_decode_extension_box(struct lua_State *L, const char **data)
{
	assert(mp_typeof(**data) == MP_EXT);
	int8_t ext_type;
	uint32_t len = mp_decode_extl(data, &ext_type);

	if (ext_type != MP_ERROR) {
		luaL_error(L, "Unsupported MsgPack extension type: %d",
			   ext_type);
		return;
	}

	struct error *err = error_unpack(data, len);
	if (err == NULL) {
		luaL_error(L, "Can not parse an error from MsgPack");
		return;
	}

	luaT_pusherror(L, err);
	return;
}

#include "say.h"

void
box_lua_init(struct lua_State *L)
{
	luaL_cdef(L, "struct txn_savepoint;");
	CTID_STRUCT_TXN_SAVEPOINT_PTR = luaL_ctypeid(L,
						     "struct txn_savepoint*");

	/*
	 * Create a table and expose it as require('box') and
	 * as _G.box.
	 */
	luaT_newmodule(L, "box", boxlib);
	lua_setfield(L, LUA_GLOBALSINDEX, "box");

	/* box.backup = {<...>} */
	luaL_findtable(L, LUA_GLOBALSINDEX, "box.backup", 0);
	luaL_setfuncs(L, boxlib_backup, 0);
	lua_pop(L, 1);

	box_lua_error_init(L);
	box_lua_tuple_init(L);
	box_lua_call_init(L);
	box_lua_cfg_init(L);
	box_lua_lib_init(L);
	box_lua_slab_init(L);
	box_lua_index_init(L);
	box_lua_space_init(L);
	box_lua_sequence_init(L);
	box_lua_misc_init(L);
	box_lua_info_init(L);
	box_lua_stat_init(L);
	box_lua_ctl_init(L);
	box_lua_session_init(L);
	box_lua_xlog_init(L);
	box_lua_sql_init(L);
	box_lua_watcher_init(L);
	box_lua_iproto_init(L);
	box_lua_space_upgrade_init(L);
	box_lua_audit_init(L);
	box_lua_wal_ext_init(L);
	box_lua_read_view_init(L);
	box_lua_security_init(L);
	box_lua_flightrec_init(L);
	luaopen_net_box(L);
	lua_pop(L, 1);
	tarantool_lua_console_init(L);
	lua_pop(L, 1);
	luaopen_key_def(L);
	lua_pop(L, 1);
	luaopen_merger(L);
	lua_pop(L, 1);

	luamp_set_encode_extension(luamp_encode_extension_box);
	luamp_set_decode_extension(luamp_decode_extension_box);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s += 3) {
		const char *modfile_raw = *s;
		const char *modname = *(s + 1);
		const char *modsrc = *(s + 2);

		const char *modfile = lua_pushfstring(L,
			"@builtin/%s.lua", modfile_raw);
		if (luaL_loadbuffer(L, modsrc, strlen(modsrc), modfile) != 0 ||
		    lua_pcall(L, 0, 1, 0) != 0)
			panic("Error loading Lua module %s...: %s",
			      modname != NULL ? modname : modfile,
			      lua_tostring(L, -1));

		/*
		 * Register a built-in module if the module name
		 * is provided. Otherwise ensure that no value is
		 * returned.
		 */
		if (modname == NULL) {
			assert(lua_isnil(L, -1));
			lua_pop(L, 1);
		} else {
			luaT_setmodule(L, modname);
		}

		lua_pop(L, 1); /* modfile */

		/*
		 * TODO: Use a module name (as written in a
		 * require() call) as the
		 * tarantool.debug.getsources() parameter.
		 *
		 * For example, "net.box" instead of
		 * "box/net_box".
		 */
		builtin_modcache_put(modfile_raw, modsrc);
	}

	assert(lua_gettop(L) == 0);
}

void
box_lua_free(void)
{
	box_lua_iproto_free();
}
