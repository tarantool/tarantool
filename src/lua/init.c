/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "lua/init.h"
#include "lua/utils.h"
#include "main.h"
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__) || defined(__OpenBSD__)
#include <libgen.h>
#endif

#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lj_cdata.h>
#include <lmisclib.h>
#include <luajit.h>

#include <fiber.h>
#include "version.h"
#include "coio.h"
#include "core/backtrace.h"
#include "core/tt_static.h"
#include "lua/backtrace.h"
#include "lua/fiber.h"
#include "lua/fiber_cond.h"
#include "lua/fiber_channel.h"
#include "lua/errno.h"
#include "lua/socket.h"
#include "lua/utils.h"
#include "lua/serializer.h"
#include <lua-cjson/lua_cjson.h>
#include <lua-yaml/lyaml.h>
#include "lua/msgpack.h"
#include "lua/pickle.h"
#include "lua/minifio.h"
#include "lua/fio.h"
#include "lua/popen.h"
#include "lua/httpc.h"
#include "lua/utf8.h"
#include "lua/swim.h"
#include "lua/decimal.h"
#include "lua/uri.h"
#include "lua/builtin_modcache.h"
#include "lua/tweaks.h"
#include "digest.h"
#include "errinj.h"

#ifdef ENABLE_BACKTRACE
#include "core/backtrace.h"
#endif

#include <small/ibuf.h>

#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

#if defined(EMBED_LUAZLIB)
LUALIB_API int
luaopen_zlib(lua_State *L);
#endif

#if defined(EMBED_LUAZIP)
LUALIB_API int
luaopen_zip(lua_State *L);
#endif

#if defined(ENABLE_COMPRESS_MODULE)
void
tarantool_lua_compress_init(lua_State *L);
#endif

#define MAX_MODNAME 64

/**
 * The single Lua state of the transaction processor (tx) thread.
 */
struct lua_State *tarantool_L;
/**
 * The fiber running the startup Lua script
 */
struct fiber *script_fiber;
bool start_loop = true;

/* contents of src/lua/ files */
extern char minifio_lua[],
	loaders_lua[],
	strict_lua[],
	compat_lua[],
	uuid_lua[],
	msgpackffi_lua[],
	fun_lua[],
	crypto_lua[],
	digest_lua[],
	debug_lua[],
	init_lua[],
	buffer_lua[],
	errno_lua[],
	fiber_lua[],
	httpc_lua[],
	log_lua[],
	uri_lua[],
	socket_lua[],
	help_lua[],
	help_en_US_lua[],
	tap_lua[],
	fio_lua[],
	error_lua[],
	argparse_lua[],
	iconv_lua[],
	/* jit.* library */
	jit_vmdef_lua[],
	jit_bc_lua[],
	jit_bcsave_lua[],
	jit_dis_arm64_lua[],
	jit_dis_x86_lua[],
	jit_dis_x64_lua[],
	jit_dump_lua[],
	dobytecode_lua[],
	dojitcmd_lua[],
	csv_lua[],
	jit_v_lua[],
	clock_lua[],
	title_lua[],
	env_lua[],
	pwd_lua[],
	table_lua[],
	trigger_lua[],
	string_lua[],
	swim_lua[],
	jit_p_lua[], /* LuaJIT 2.1 profiler */
	jit_zone_lua[], /* LuaJIT 2.1 profiler */
	/* tools.* libraries. */
	utils_avl_lua[],
	utils_bufread_lua[],
	utils_symtab_lua[],
	memprof_parse_lua[],
	memprof_process_lua[],
	memprof_humanize_lua[],
	memprof_lua[],
	sysprof_parse_lua[],
	sysprof_collapse_lua[],
	sysprof_lua[],
	datetime_lua[],
	timezones_lua[],
	print_lua[],
	pairs_lua[],
	luadebug_lua[]
#if defined(ENABLE_COMPRESS_MODULE)
	, compress_lua[]
#endif
#if defined(EMBED_LUAROCKS)
	, luarocks_core_hardcoded_lua[],
	luarocks_admin_cache_lua[],
	luarocks_admin_cmd_add_lua[],
	luarocks_admin_cmd_make_manifest_lua[],
	luarocks_admin_cmd_refresh_cache_lua[],
	luarocks_admin_cmd_remove_lua[],
	luarocks_admin_index_lua[],
	luarocks_build_builtin_lua[],
	luarocks_build_cmake_lua[],
	luarocks_build_command_lua[],
	luarocks_build_lua[],
	luarocks_build_make_lua[],
	luarocks_cmd_build_lua[],
	luarocks_cmd_config_lua[],
	luarocks_cmd_doc_lua[],
	luarocks_cmd_download_lua[],
	luarocks_cmd_help_lua[],
	luarocks_cmd_init_lua[],
	luarocks_cmd_install_lua[],
	luarocks_cmd_lint_lua[],
	luarocks_cmd_list_lua[],
	luarocks_cmd_lua[],
	luarocks_cmd_make_lua[],
	luarocks_cmd_new_version_lua[],
	luarocks_cmd_pack_lua[],
	luarocks_cmd_path_lua[],
	luarocks_cmd_purge_lua[],
	luarocks_cmd_remove_lua[],
	luarocks_cmd_search_lua[],
	luarocks_cmd_show_lua[],
	luarocks_cmd_test_lua[],
	luarocks_cmd_unpack_lua[],
	luarocks_cmd_upload_lua[],
	luarocks_cmd_which_lua[],
	luarocks_cmd_write_rockspec_lua[],
	luarocks_core_cfg_lua[],
	luarocks_core_dir_lua[],
	luarocks_core_manif_lua[],
	luarocks_core_path_lua[],
	luarocks_core_persist_lua[],
	luarocks_core_sysdetect_lua[],
	luarocks_core_vers_lua[],
	luarocks_deps_lua[],
	luarocks_dir_lua[],
	luarocks_download_lua[],
	luarocks_fetch_cvs_lua[],
	luarocks_fetch_git_file_lua[],
	luarocks_fetch_git_http_lua[],
	luarocks_fetch_git_https_lua[],
	luarocks_fetch_git_lua[],
	luarocks_fetch_git_ssh_lua[],
	luarocks_fetch_hg_http_lua[],
	luarocks_fetch_hg_https_lua[],
	luarocks_fetch_hg_lua[],
	luarocks_fetch_hg_ssh_lua[],
	luarocks_signing_lua[],
	luarocks_fetch_lua[],
	luarocks_fetch_sscm_lua[],
	luarocks_fetch_svn_lua[],
	luarocks_fs_lua[],
	luarocks_fs_lua_lua[],
	luarocks_fs_tools_lua[],
	luarocks_fs_unix_lua[],
	luarocks_fs_unix_tools_lua[],
	luarocks_fun_lua[],
	luarocks_loader_lua[],
	luarocks_manif_lua[],
	luarocks_manif_writer_lua[],
	luarocks_pack_lua[],
	luarocks_path_lua[],
	luarocks_persist_lua[],
	luarocks_queries_lua[],
	luarocks_remove_lua[],
	luarocks_repos_lua[],
	luarocks_require_lua[],
	luarocks_results_lua[],
	luarocks_rockspecs_lua[],
	luarocks_search_lua[],
	luarocks_test_busted_lua[],
	luarocks_test_command_lua[],
	luarocks_test_lua[],
	luarocks_tools_patch_lua[],
	luarocks_tools_tar_lua[],
	luarocks_tools_zip_lua[],
	luarocks_type_check_lua[],
	luarocks_type_manifest_lua[],
	luarocks_type_rockspec_lua[],
	luarocks_upload_api_lua[],
	luarocks_upload_multipart_lua[],
	luarocks_util_lua[],
	luarocks_core_util_lua[]
#endif /* defined(EMBED_LUAROCKS) */
;

static const char *lua_modules[] = {
	/* Make it first to affect load of all other modules */
	"strict", strict_lua,
	"compat", compat_lua,
	"fun", fun_lua,
	"debug", debug_lua,
	"tarantool", init_lua,
	"errno", errno_lua,
	"fiber", fiber_lua,
	"env", env_lua,
	"buffer", buffer_lua,
	"string", string_lua,
	"table", table_lua,
	"msgpackffi", msgpackffi_lua,
	"crypto", crypto_lua,
	"digest", digest_lua,
	"uuid", uuid_lua,
	"log", log_lua,
	"uri", uri_lua,
	"fio", fio_lua,
	"error", error_lua,
	"csv", csv_lua,
	"clock", clock_lua,
	"socket", socket_lua,
	"title", title_lua,
	"tap", tap_lua,
	"help.en_US", help_en_US_lua,
	"help", help_lua,
	"internal.argparse", argparse_lua,
	"internal.trigger", trigger_lua,
	"pwd", pwd_lua,
	"http.client", httpc_lua,
	"iconv", iconv_lua,
	"swim", swim_lua,
#if defined(ENABLE_COMPRESS_MODULE)
	"compress", compress_lua,
#endif
	/* jit.* library */
	"jit.vmdef", jit_vmdef_lua,
	"jit.bc", jit_bc_lua,
	"jit.bcsave", jit_bcsave_lua,
	"jit.dis_arm64", jit_dis_arm64_lua,
	"jit.dis_x86", jit_dis_x86_lua,
	"jit.dis_x64", jit_dis_x64_lua,
	"jit.dump", jit_dump_lua,
	"jit.v", jit_v_lua,
	"internal.dobytecode", dobytecode_lua,
	"internal.dojitcmd", dojitcmd_lua,
	/* Profiler */
	"jit.p", jit_p_lua,
	"jit.zone", jit_zone_lua,
	/* tools.* libraries. Order is important. */
	"utils.avl", utils_avl_lua,
	"utils.bufread", utils_bufread_lua,
	"utils.symtab", utils_symtab_lua,
	"memprof.parse", memprof_parse_lua,
	"memprof.process", memprof_process_lua,
	"memprof.humanize", memprof_humanize_lua,
	"memprof", memprof_lua,
	"sysprof.parse", sysprof_parse_lua,
	"sysprof.collapse", sysprof_collapse_lua,
	"sysprof", sysprof_lua,
	"timezones", timezones_lua,
	"datetime", datetime_lua,
	"internal.print", print_lua,
	"internal.pairs", pairs_lua,
	"luadebug", luadebug_lua,
	NULL
};

/**
 * If there's a risk that a module may fail to load, put it here.
 * Then it'll be embedded, but not loaded until the first use.
 */
static const char *lua_modules_preload[] = {
#if defined(EMBED_LUAROCKS)
	/*
	 * LuaRocks creates a temporary file on startup, which may fail
	 * if the tmp dir is read-only.
	 */
	"luarocks.core.hardcoded", luarocks_core_hardcoded_lua,
	"luarocks.core.util", luarocks_core_util_lua,
	"luarocks.core.persist", luarocks_core_persist_lua,
	"luarocks.core.sysdetect", luarocks_core_sysdetect_lua,
	"luarocks.core.cfg", luarocks_core_cfg_lua,
	"luarocks.core.dir", luarocks_core_dir_lua,
	"luarocks.core.path", luarocks_core_path_lua,
	"luarocks.core.manif", luarocks_core_manif_lua,
	"luarocks.core.vers", luarocks_core_vers_lua,
	"luarocks.util", luarocks_util_lua,
	"luarocks.loader", luarocks_loader_lua,
	"luarocks.dir", luarocks_dir_lua,
	"luarocks.path", luarocks_path_lua,
	"luarocks.fs", luarocks_fs_lua,
	"luarocks.persist", luarocks_persist_lua,
	"luarocks.fun", luarocks_fun_lua,
	"luarocks.tools.patch", luarocks_tools_patch_lua,
	"luarocks.tools.zip", luarocks_tools_zip_lua,
	"luarocks.tools.tar", luarocks_tools_tar_lua,
	"luarocks.fs.unix", luarocks_fs_unix_lua,
	"luarocks.fs.unix.tools", luarocks_fs_unix_tools_lua,
	"luarocks.fs.lua", luarocks_fs_lua_lua,
	"luarocks.fs.tools", luarocks_fs_tools_lua,
	"luarocks.queries", luarocks_queries_lua,
	"luarocks.type_check", luarocks_type_check_lua,
	"luarocks.type.rockspec", luarocks_type_rockspec_lua,
	"luarocks.rockspecs", luarocks_rockspecs_lua,
	"luarocks.signing", luarocks_signing_lua,
	"luarocks.fetch", luarocks_fetch_lua,
	"luarocks.type.manifest", luarocks_type_manifest_lua,
	"luarocks.manif", luarocks_manif_lua,
	"luarocks.build.builtin", luarocks_build_builtin_lua,
	"luarocks.deps", luarocks_deps_lua,
	"luarocks.cmd", luarocks_cmd_lua,
	"luarocks.test.busted", luarocks_test_busted_lua,
	"luarocks.test.command", luarocks_test_command_lua,
	"luarocks.results", luarocks_results_lua,
	"luarocks.search", luarocks_search_lua,
	"luarocks.repos", luarocks_repos_lua,
	"luarocks.cmd.show", luarocks_cmd_show_lua,
	"luarocks.cmd.path", luarocks_cmd_path_lua,
	"luarocks.cmd.write_rockspec", luarocks_cmd_write_rockspec_lua,
	"luarocks.manif.writer", luarocks_manif_writer_lua,
	"luarocks.remove", luarocks_remove_lua,
	"luarocks.pack", luarocks_pack_lua,
	"luarocks.build", luarocks_build_lua,
	"luarocks.cmd.make", luarocks_cmd_make_lua,
	"luarocks.cmd.build", luarocks_cmd_build_lua,
	"luarocks.cmd.install", luarocks_cmd_install_lua,
	"luarocks.cmd.list", luarocks_cmd_list_lua,
	"luarocks.download", luarocks_download_lua,
	"luarocks.cmd.download", luarocks_cmd_download_lua,
	"luarocks.cmd.search", luarocks_cmd_search_lua,
	"luarocks.cmd.pack", luarocks_cmd_pack_lua,
	"luarocks.cmd.new_version", luarocks_cmd_new_version_lua,
	"luarocks.cmd.purge", luarocks_cmd_purge_lua,
	"luarocks.cmd.init", luarocks_cmd_init_lua,
	"luarocks.cmd.lint", luarocks_cmd_lint_lua,
	"luarocks.test", luarocks_test_lua,
	"luarocks.cmd.test", luarocks_cmd_test_lua,
	"luarocks.cmd.which", luarocks_cmd_which_lua,
	"luarocks.cmd.remove", luarocks_cmd_remove_lua,
	"luarocks.upload.multipart", luarocks_upload_multipart_lua,
	"luarocks.upload.api", luarocks_upload_api_lua,
	"luarocks.cmd.upload", luarocks_cmd_upload_lua,
	"luarocks.cmd.help", luarocks_cmd_help_lua,
	"luarocks.cmd.doc", luarocks_cmd_doc_lua,
	"luarocks.cmd.unpack", luarocks_cmd_unpack_lua,
	"luarocks.cmd.config", luarocks_cmd_config_lua,
	"luarocks.require", luarocks_require_lua,
	"luarocks.build.cmake", luarocks_build_cmake_lua,
	"luarocks.build.make", luarocks_build_make_lua,
	"luarocks.build.command", luarocks_build_command_lua,
	"luarocks.fetch.cvs", luarocks_fetch_cvs_lua,
	"luarocks.fetch.svn", luarocks_fetch_svn_lua,
	"luarocks.fetch.sscm", luarocks_fetch_sscm_lua,
	"luarocks.fetch.git", luarocks_fetch_git_lua,
	"luarocks.fetch.git_file", luarocks_fetch_git_file_lua,
	"luarocks.fetch.git_http", luarocks_fetch_git_http_lua,
	"luarocks.fetch.git_https", luarocks_fetch_git_https_lua,
	"luarocks.fetch.git_ssh", luarocks_fetch_git_ssh_lua,
	"luarocks.fetch.hg", luarocks_fetch_hg_lua,
	"luarocks.fetch.hg_http", luarocks_fetch_hg_http_lua,
	"luarocks.fetch.hg_https", luarocks_fetch_hg_https_lua,
	"luarocks.fetch.hg_ssh", luarocks_fetch_hg_ssh_lua,
	"luarocks.admin.cache", luarocks_admin_cache_lua,
	"luarocks.admin.cmd.refresh_cache", luarocks_admin_cmd_refresh_cache_lua,
	"luarocks.admin.index", luarocks_admin_index_lua,
	"luarocks.admin.cmd.add", luarocks_admin_cmd_add_lua,
	"luarocks.admin.cmd.remove", luarocks_admin_cmd_remove_lua,
	"luarocks.admin.cmd.make_manifest", luarocks_admin_cmd_make_manifest_lua,
#endif /* defined(EMBED_LUAROCKS) */
	NULL
};

/*
 * {{{ box Lua library: common functions
 */

/*
 * Retrieve builtin module sources, if available.
 */
static const char *
tarantool_debug_getsources(const char *modname)
{
	/* 1. process as short modname - fast path */
	const char *lua_code = builtin_modcache_find(modname);
	if (lua_code != NULL)
		return lua_code;
	size_t n = strlen(modname);
	assert(n < MAX_MODNAME);
	char fullname[MAX_MODNAME];
	strlcpy(fullname, modname, n + 1);

	/* 2. slow path: process as `@builtin/%s.lua` */
	/* check prefix first */
	const char *prefix = "@builtin/";
	const size_t prefix_len = strlen(prefix);
	if (strncmp(prefix, fullname, prefix_len) != 0)
		return NULL;
	const char *pshortname = fullname + prefix_len;

	/* trim suffix 2nd */
	const char *suffix = ".lua";
	const size_t suffix_len = strlen(suffix);
	if (strncmp(suffix, fullname + n - suffix_len, suffix_len) != 0)
		return NULL;
	fullname[n - suffix_len] = '\0';

	return builtin_modcache_find(pshortname);
}

/*
 * LuaC implementation of a function to retrieve builtin module sources.
 */
static int
lbox_tarantool_debug_getsources(struct lua_State *L)
{
	int index = lua_gettop(L);
	if (index != 1)
		luaL_error(L, "getsources() function expects one argument");
	size_t len = 0;
	const char *modname = luaL_checklstring(L, index, &len);
	if (len <= 0)
		goto ret_nil;
	const char *code = tarantool_debug_getsources(modname);
	if (code == NULL)
		goto ret_nil;
	lua_pushstring(L, code);
	return 1;
ret_nil:
	lua_pushnil(L);
	return 1;
}

/**
 * Convert lua number or string to lua cdata 64bit number.
 */
static int
lbox_tonumber64(struct lua_State *L)
{
	luaL_checkany(L, 1);
	int base = luaL_optint(L, 2, -1);
	luaL_argcheck(L, (2 <= base && base <= 36) || base == -1, 2,
		      "base out of range");
	switch (lua_type(L, 1)) {
	case LUA_TNUMBER:
		base = (base == -1 ? 10 : base);
		if (base != 10)
			return luaL_argerror(L, 1, "string expected");
		lua_settop(L, 1); /* return original value as is */
		return 1;
	case LUA_TSTRING:
	{
		size_t argl = 0;
		const char *arg = luaL_checklstring(L, 1, &argl);
		/* Trim whitespaces at begin/end */
		while (argl > 0 && isspace(arg[argl - 1])) {
			argl--;
		}
		while (isspace(*arg)) {
			arg++; argl--;
		}

		/*
		 * Check if we're parsing custom format:
		 * 1) '0x' or '0X' trim in case of base == 16 or base == -1
		 * 2) '0b' or '0B' trim in case of base == 2  or base == -1
		 * 3) '-' for negative numbers
		 * 4) LL, ULL, LLU - trim, but only for base == 2 or
		 *    base == 16 or base == -1. For consistency do not bother
		 *    with any non-common bases, since user may have specified
		 *    base >= 22, in which case 'L' will be a digit.
		 */
		char negative = 0;
		if (arg[0] == '-') {
			arg++; argl--;
			negative = 1;
		}
		if (argl > 2 && arg[0] == '0') {
			if ((arg[1] == 'x' || arg[1] == 'X') &&
			    (base == 16 || base == -1)) {
				base = 16; arg += 2; argl -= 2;
			} else if ((arg[1] == 'b' || arg[1] == 'B') &&
			           (base == 2 || base == -1)) {
				base = 2;  arg += 2; argl -= 2;
			}
		}
		bool ull = false;
		if (argl > 2 && (base == 2 || base == 16 || base == -1)) {
			if (arg[argl - 1] == 'u' || arg[argl - 1] == 'U') {
				ull = true;
				--argl;
			}
			if ((arg[argl - 1] == 'l' || arg[argl - 1] == 'L') &&
			    (arg[argl - 2] == 'l' || arg[argl - 2] == 'L'))
				argl -= 2;
			else {
				ull = false;
				goto skip;
			}
			if (!ull && (arg[argl - 1] == 'u' ||
				     arg[argl - 1] == 'U')) {
				ull = true;
				--argl;
			}
		}
skip:		base = (base == -1 ? 10 : base);
		errno = 0;
		char *arge;
		unsigned long long result = strtoull(arg, &arge, base);
		if (errno == 0 && arge == arg + argl) {
			if (argl == 0) {
				lua_pushnil(L);
			} else if (negative) {
				/*
				 * To test overflow, consider
				 *  result > -INT64_MIN;
				 *  result - 1 > -INT64_MIN - 1;
				 * Assumption:
				 *  INT64_MAX == -(INT64_MIN + 1);
				 * Finally,
				 *  result - 1 > INT64_MAX;
				 */
				if (ull)
					luaL_pushuint64(L, (UINT64_MAX - result) + 1);
				else if (result != 0 && result - 1 > INT64_MAX)
					lua_pushnil(L);
				else
					luaL_pushint64(L, -result);
			} else {
				luaL_pushuint64(L, result);
			}
			return 1;
		}
		break;
	} /* LUA_TSTRING */
	case LUA_TCDATA:
	{
		base = (base == -1 ? 10 : base);
		if (base != 10)
			return luaL_argerror(L, 1, "string expected");
		uint32_t ctypeid = 0;
		luaL_checkcdata(L, 1, &ctypeid);
		if (ctypeid >= CTID_INT8 && ctypeid <= CTID_DOUBLE) {
			lua_pushvalue(L, 1);
			return 1;
		}
		break;
	} /* LUA_TCDATA */
	}
	lua_pushnil(L);
	return 1;
}

/* }}} */

#ifdef ENABLE_BACKTRACE
/**
 * Backtracing function for sysprof.
 **/
static void
fiber_backtracer(void *(*frame_writer)(int frame_no, void *addr))
{
	struct backtrace bt = {};
	int frame_no;
	const struct fiber *cur = fiber_self();
	backtrace_collect(&bt, cur, 0);
	for (frame_no = 0; frame_no < bt.frame_count; ++frame_no) {
		frame_writer(frame_no, bt.frames[frame_no].ip);
	}
}
#endif

/**
 * Original LuaJIT/Lua logic: <luajit/src/lib_package.c - function setpath>
 *
 * 1) If environment variable 'envname' is empty, it uses only <default value>
 * 2) Otherwise:
 *    - If it contains ';;', then ';;' is replaced with ';'<default value>';'
 *    - Otherwise is uses only what's inside this value.
 **/
static void
tarantool_lua_pushpath_env(struct lua_State *L, const char *envname)
{
	char *path = getenv_safe(envname, NULL, 0);
	if (path != NULL) {
		const char *def = lua_tostring(L, -1);
		const char *path_new = luaL_gsub(L, path, ";;", ";\1;");
		free(path);
		luaL_gsub(L, path_new, "\1", def);
		lua_remove(L, -2);
		lua_remove(L, -2);
	}
}

/**
 * Prepend the variable list of arguments to the Lua
 * package search path
 */
static void
tarantool_lua_setpaths(struct lua_State *L)
{
	char *home = getenv_safe("HOME", NULL, 0);
	lua_getglobal(L, "package");
	int top = lua_gettop(L);

	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/5.1/?.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/5.1/?/init.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/?.lua;");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/share/lua/?/init.lua;");
	}
	lua_pushliteral(L, MODULE_LUAPATH ";");
	/* overwrite standard paths */
	lua_concat(L, lua_gettop(L) - top);
	tarantool_lua_pushpath_env(L, "LUA_PATH");
	lua_setfield(L, top, "path");

	if (home != NULL) {
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/5.1/?" MODULE_LIBSUFFIX ";");
		lua_pushstring(L, home);
		lua_pushliteral(L, "/.luarocks/lib/lua/?" MODULE_LIBSUFFIX ";");
		free(home);
	}
	lua_pushliteral(L, MODULE_LIBPATH ";");
	/* overwrite standard paths */
	lua_concat(L, lua_gettop(L) - top);
	tarantool_lua_pushpath_env(L, "LUA_CPATH");
	lua_setfield(L, top, "cpath");

	assert(lua_gettop(L) == top);
	lua_pop(L, 1); /* package */
}

static int
tarantool_panic_handler(lua_State *L) {
	const char *problem = lua_tostring(L, -1);
#ifdef ENABLE_BACKTRACE
	struct backtrace bt;
	backtrace_collect(&bt, fiber(), 1);
	backtrace_print(&bt, STDERR_FILENO);
#endif /* ENABLE_BACKTRACE */
	say_crit("%s", problem);
	int level = 1;
	lua_Debug ar;
	while (lua_getstack(L, level++, &ar) == 1) {
		if (lua_getinfo(L, "nSl", &ar) == 0)
			break;
		say_crit("#%d %s (%s), %s:%d", level,
			 ar.name, ar.namewhat,
			 ar.short_src, ar.currentline);
	}
	return 1;
}

static int
luaopen_tarantool(lua_State *L)
{
	/* Set _G._TARANTOOL (like _VERSION) */
	lua_pushstring(L, tarantool_version());
	lua_setfield(L, LUA_GLOBALSINDEX, "_TARANTOOL");

	/*
	 * Get tarantool module.
	 *
	 * src/lua/init.lua is already registered as tarantool
	 * module, so we `require` it here, not create.
	 */
	lua_getfield(L, LUA_GLOBALSINDEX, "require");
	lua_pushstring(L, "tarantool");
	lua_call(L, 1, 1);

	/* package */
	lua_pushstring(L, tarantool_package());
	lua_setfield(L, -2, "package");

	/* version */
	lua_pushstring(L, tarantool_version());
	lua_setfield(L, -2, "version");

	/* build */
	lua_pushstring(L, "build");
	lua_newtable(L);

	/* build.target */
	lua_pushstring(L, "target");
	lua_pushstring(L, BUILD_INFO);
	lua_settable(L, -3);

	/* build.options */
	lua_pushstring(L, "options");
	lua_pushstring(L, BUILD_OPTIONS);
	lua_settable(L, -3);

	/* build.compiler */
	lua_pushstring(L, "compiler");
	lua_pushstring(L, COMPILER_INFO);
	lua_settable(L, -3);

	/* build.mod_format */
	lua_pushstring(L, "mod_format");
	lua_pushstring(L, TARANTOOL_LIBEXT);
	lua_settable(L, -3);

	/* build.flags */
	lua_pushstring(L, "flags");
	lua_pushstring(L, TARANTOOL_C_FLAGS);
	lua_settable(L, -3);

	/* build.linking */
	lua_pushstring(L, "linking");
#if defined(BUILD_STATIC)
	lua_pushstring(L, "static");
#else
	lua_pushstring(L, "dynamic");
#endif
	lua_settable(L, -3);

	lua_settable(L, -3);    /* box.info.build */

	/* debug */
	lua_newtable(L);
	lua_pushcfunction(L, lbox_tarantool_debug_getsources);
	lua_setfield(L, -2, "getsources");
	lua_setfield(L, -2, "debug");
	lua_pop(L, 1);
	return 1;
}

/**
 * Load Lua code from a string and register a built-in module.
 */
static void
luaT_set_module_from_source(struct lua_State *L, const char *modname,
			    const char *modsrc)
{
	/*
	 * TODO: Use a file name, not a module name for the
	 * luaL_loadbuffer() parameter.
	 *
	 * For example, "@builtin/argparse.lua" instead of
	 * "@builtin/internal.argparse.lua".
	 */
	const char *modfile = lua_pushfstring(L, "@builtin/%s.lua", modname);
	if (luaL_loadbuffer(L, modsrc, strlen(modsrc), modfile))
		panic("Error loading Lua module %s...: %s",
		      modname, lua_tostring(L, -1));
	lua_pushstring(L, modname);
	lua_call(L, 1, 1);

	luaT_setmodule(L, modname);

	builtin_modcache_put(modname, modsrc);
	lua_pop(L, 1); /* modfile */
}

void
tarantool_lua_init(const char *tarantool_bin, int argc, char **argv)
{
	lua_State *L = luaL_newstate();
	if (L == NULL) {
		panic("failed to initialize Lua");
	}
	luaL_openlibs(L);

	/*
	 * Create a table for storing loaded built-in modules.
	 * Similar to _LOADED (package.loaded).
	 */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "_TARANTOOL_BUILTIN");

	builtin_modcache_init();

	/*
	 * Setup paths and loaders.
	 *
	 * Load minifio first, because the loaders module depends
	 * on it.
	 */
	tarantool_lua_setpaths(L);
	tarantool_lua_minifio_init(L);
	luaT_set_module_from_source(L, "internal.minifio", minifio_lua);
	luaT_set_module_from_source(L, "internal.loaders", loaders_lua);

	/* Initialize ffi to enable luaL_pushcdata/luaL_checkcdata functions */
	luaL_loadstring(L, "return require('ffi')");
	lua_call(L, 0, 0);
	lua_register(L, "tonumber64", lbox_tonumber64);

	tarantool_lua_tweaks_init(L);
	tarantool_lua_uri_init(L);
	tarantool_lua_utf8_init(L);
	tarantool_lua_utils_init(L);
	tarantool_lua_fiber_init(L);
	tarantool_lua_fiber_cond_init(L);
	tarantool_lua_fiber_channel_init(L);
	tarantool_lua_errno_init(L);
	tarantool_lua_error_init(L);
	tarantool_lua_fio_init(L);
	tarantool_lua_popen_init(L);
	tarantool_lua_socket_init(L);
	tarantool_lua_pickle_init(L);
	tarantool_lua_digest_init(L);
	tarantool_lua_serializer_init(L);
	tarantool_lua_swim_init(L);
	tarantool_lua_decimal_init(L);
#if defined(ENABLE_COMPRESS_MODULE)
	tarantool_lua_compress_init(L);
#endif
#ifdef ENABLE_BACKTRACE
	luaM_sysprof_set_backtracer(fiber_backtracer);
#endif
	luaopen_http_client_driver(L);
	lua_pop(L, 1);
	luaopen_msgpack(L);
	lua_pop(L, 1);
	luaopen_yaml(L);
	lua_pop(L, 1);
	luaopen_json(L);
	lua_pop(L, 1);
#if defined(EMBED_LUAZLIB)
	luaopen_zlib(L);
	lua_pop(L, 1);
#endif
#if defined(EMBED_LUAZIP)
	luaopen_zip(L);
	lua_pop(L, 1);
#endif
#if defined(HAVE_GNU_READLINE)
	/*
	 * Disable libreadline signals handlers. All signals are handled in
	 * main thread by libev watchers.
	 */
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
#endif
#if defined(ENABLE_BACKTRACE)
	backtrace_lua_init();
#endif /* defined(ENABLE_BACKTRACE) */
	for (const char **s = lua_modules; *s; s += 2) {
		const char *modname = *s;
		const char *modsrc = *(s + 1);
		luaT_set_module_from_source(L, modname, modsrc);
	}

	lua_getfield(L, LUA_REGISTRYINDEX, "_PRELOAD");
	for (const char **s = lua_modules_preload; *s; s += 2) {
		const char *modname = *s;
		const char *modsrc = *(s + 1);
		const char *modfile = lua_pushfstring(L,
			"@builtin/%s.lua", modname);
		if (luaL_loadbuffer(L, modsrc, strlen(modsrc), modfile))
			panic("Error loading Lua module %s...: %s",
			      modname, lua_tostring(L, -1));
		lua_setfield(L, -3, modname); /* package.preload.modname = t */
		lua_pop(L, 1); /* chunkname */
		builtin_modcache_put(modname, modsrc);
	}
	lua_pop(L, 1); /* _PRELOAD */

	luaopen_tarantool(L);

	lua_newtable(L);
	lua_pushinteger(L, -1);
	lua_pushstring(L, tarantool_bin);
	lua_settable(L, -3);
	for (int i = 0; i < argc; i++) {
		lua_pushinteger(L, i);
		lua_pushstring(L, argv[i]);
		lua_settable(L, -3);
	}
	lua_setfield(L, LUA_GLOBALSINDEX, "arg");

#ifdef NDEBUG
	/* Unload strict after boot in release mode */
	if (luaL_dostring(L, "require('strict').off()") != 0)
		panic("Failed to unload 'strict' Lua module");
#endif /* NDEBUG */

	lua_atpanic(L, tarantool_panic_handler);
	/* clear possible left-overs of init */
	lua_settop(L, 0);
	tarantool_L = L;
}

char *history = NULL;

struct slab_cache *
tarantool_lua_slab_cache()
{
	return &cord()->slabc;
}

/**
 * Import a lua module and push it on top of Lua stack.
 */
static int
lua_require_lib(lua_State *L, const char *libname)
{
	assert(libname != NULL);
	lua_getglobal(L, "require");
	lua_pushstring(L, libname);
	return luaT_call(L, 1, 1);
}

/**
 * Push argument and call a function on the top of Lua stack
 */
static int
lua_main(lua_State *L, int argc, char **argv)
{
	assert(lua_isfunction(L, -1));
	lua_checkstack(L, argc - 1);
	for (int i = 1; i < argc; i++)
		lua_pushstring(L, argv[i]);
	int rc = luaT_call(L, lua_gettop(L) - 1, 0);
	/* clear the stack from return values. */
	lua_settop(L, 0);
	return rc;
}

/**
 * Execute start-up script.
 */
static int
run_script_f(va_list ap)
{
	struct lua_State *L = va_arg(ap, struct lua_State *);
	const char *path = va_arg(ap, const char *);
	uint32_t opt_mask = va_arg(ap, uint32_t);
	int optc = va_arg(ap, int);
	const char **optv = va_arg(ap, const char **);
	int argc = va_arg(ap, int);
	char **argv = va_arg(ap, char **);
	bool interactive = opt_mask & O_INTERACTIVE;
	bool bytecode = opt_mask & O_BYTECODE;
	/*
	 * An error is returned via an external diag. A caller
	 * can't use fiber_join(), because the script can call
	 * os.exit(). That call makes the script runner fiber
	 * never really dead. It never returns from its function.
	 */
	struct diag *diag = va_arg(ap, struct diag *);
	bool aux_loop_is_run = false;
	bool is_option_e_ran = false;

	/*
	 * Execute scripts or modules pointed by TT_PRELOAD
	 * environment variable.
	 */
	lua_getfield(L, LUA_GLOBALSINDEX, "require");
	lua_pushstring(L, "tarantool");
	if (luaT_call(L, 1, 1) != 0)
		goto error;
	lua_getfield(L, -1, "_internal");
	lua_getfield(L, -1, "run_preload");
	if (luaT_call(L, 0, 0) != 0)
		goto error;
	lua_settop(L, 0);

	/*
	 * Load libraries and execute chunks passed by -l and -e
	 * command line options
	 */
	for (int i = 0; i < optc; i += 2) {
		assert(optv[i][0] == '-' && optv[i][2] == '\0');
		switch (optv[i][1]) {
		case 'l':
			/*
			 * Load library
			 */
			if (lua_require_lib(L, optv[i + 1]) != 0)
				goto error;
			/* Non-standard: set name = require('name') */
			lua_setglobal(L, optv[i + 1]);
			lua_settop(L, 0);
			break;
		case 'j':
			if (lua_require_lib(L, "internal.dojitcmd") != 0)
				goto error;
			lua_pushstring(L, "dojitcmd");
			lua_gettable(L, -2);
			lua_pushstring(L, optv[i + 1]);
			if (luaT_call(L, 1, 1) != 0)
				goto error;
			lua_settop(L, 0);
			break;
		case 'e':
			/*
			 * Execute chunk
			 */
			if (luaL_loadbuffer(L, optv[i + 1], strlen(optv[i + 1]),
					    "=(command line)") != 0)
				goto luajit_error;
			if (luaT_call(L, 0, 0) != 0)
				goto error;
			lua_settop(L, 0);
			is_option_e_ran = true;
			break;
		default:
			unreachable(); /* checked by getopt() in main() */
		}
	}

	/*
	 * Return control to tarantool_lua_run_script.
	 * tarantool_lua_run_script then will start an auxiliary event
	 * loop and re-schedule this fiber.
	 */
	fiber_sleep(0.0);
	aux_loop_is_run = true;

	int is_a_tty = isatty(STDIN_FILENO);

	if (bytecode) {
		if (lua_require_lib(L, "internal.dobytecode") != 0)
			goto error;
		lua_pushstring(L, "dobytecode");
		lua_gettable(L, -2);
		for (int i = 0; i < argc; i++)
			lua_pushstring(L, argv[i]);
		if (luaT_call(L, argc, 1) != 0)
			goto error;
		lua_settop(L, 0);
		goto end;
	}

	if (path && strcmp(path, "-") != 0 && access(path, F_OK) == 0) {
		/* Execute script. */
		if (luaL_loadfile(L, path) != 0)
			goto luajit_error;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	} else if ((!interactive && !is_a_tty) ||
			(path && strcmp(path, "-") == 0)) {
		/* Execute stdin */
		if (luaL_loadfile(L, NULL) != 0)
			goto luajit_error;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	} else if (!is_option_e_ran) {
		interactive = true;
	}

	/*
	 * Start interactive mode in any of the cases:
	 * - it was explicitly requested by "-i" option;
	 * - stdin is TTY and there are no script (-e is considered as a script).
	 */
	if (interactive) {
		say_crit("%s %s\ntype 'help' for interactive help",
			 tarantool_package(), tarantool_version());
		/* get console.start */
		lua_getfield(L, LUA_GLOBALSINDEX, "require");
		lua_pushstring(L, "console");
		if (luaT_call(L, 1, 1) != 0)
			goto error;
		lua_getfield(L, -1, "start");
		lua_remove(L, -2); /* remove console */
		start_loop = false;
		if (lua_main(L, argc, argv) != 0)
			goto error;
	}
	/*
	 * Lua script finished. Stop the auxiliary event loop and
	 * return control back to tarantool_lua_run_script.
	 */
end:
	/*
	 * Auxiliary loop in tarantool_lua_run_script
	 * should start (ev_run()) before this fiber
	 * invokes ev_break().
	 */
	if (!aux_loop_is_run)
		fiber_sleep(0.0);
	ev_break(loop(), EVBREAK_ALL);
	return 0;

luajit_error:
	diag_set(LuajitError, lua_tostring(L, -1));
error:
	diag_move(diag_get(), diag);
	goto end;
}

int
tarantool_lua_run_script(char *path, uint32_t opt_mask,
			 int optc, const char **optv, int argc, char **argv)
{
	const char *title = path ? basename(path) : "interactive";
	/*
	 * init script can call box.fiber.yield (including implicitly via
	 * box.insert, box.update, etc...), but box.fiber.yield() today,
	 * when called from 'sched' fiber crashes the server.
	 * To work this problem around we must run init script in
	 * a separate fiber.
	 */

	script_fiber = fiber_new(title, run_script_f);
	if (script_fiber == NULL)
		panic("%s", diag_last_error(diag_get())->errmsg);
	script_fiber->storage.lua.stack = tarantool_L;
	/*
	 * Create a new diag on the stack. Don't pass fiber's diag, because it
	 * might be overwritten by libev callbacks invoked in the scheduler
	 * fiber (which is this), and therefore can't be used as a sign of fail
	 * in the script itself.
	 */
	struct diag script_diag;
	diag_create(&script_diag);
	fiber_start(script_fiber, tarantool_L, path, opt_mask,
		    optc, optv, argc, argv, &script_diag);

	/*
	 * Run an auxiliary event loop to re-schedule run_script fiber.
	 * When this fiber finishes, it will call ev_break to stop the loop.
	 */
	if (start_loop)
		ev_run(loop(), 0);
	/* The fiber running the startup script has ended. */
	script_fiber = NULL;
	diag_move(&script_diag, diag_get());
	diag_destroy(&script_diag);
	/*
	 * Result can't be obtained via fiber_join - script fiber
	 * never dies if os.exit() was called. This is why diag
	 * is checked explicitly.
	 */
	return diag_is_empty(diag_get()) ? 0 : -1;
}

void
tarantool_lua_free()
{
	builtin_modcache_free();
	tarantool_lua_utf8_free();
	/*
	 * Some part of the start script panicked, and called
	 * exit().  The call stack in this case leads us back to
	 * luaL_call() in run_script(). Trying to free a Lua state
	 * from within luaL_call() is not the smartest idea (@sa
	 * gh-612).
	 */
	if (script_fiber)
		return;
	/*
	 * Got to be done prior to anything else, since GC
	 * handlers can refer to other subsystems (e.g. fibers).
	 */
	if (tarantool_L) {
		/* collects garbage, invoking userdata gc */
		lua_close(tarantool_L);
	}
	tarantool_L = NULL;

#if 0
	/* Temporarily moved to tarantool_free(), tarantool_lua_free() not
	 * being called due to cleanup order issues
	 */
	if (isatty(STDIN_FILENO)) {
		/*
		 * Restore terminal state. Doesn't hurt if exiting not
		 * due to a signal.
		 */
		rl_cleanup_after_signal();
	}
#endif
}
