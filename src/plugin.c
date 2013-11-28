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
#include "tarantool/plugin.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#include <string.h>
#include "say.h"

static RLIST_HEAD(loaded_plugins);

/**
 * Iterate over all loaded plug-ins.
 */
int
plugin_foreach(plugin_foreach_cb cb, void *cb_ctx)
{
	int res;
	struct tarantool_plugin *p;
	rlist_foreach_entry(p, &loaded_plugins, list) {
		res = cb(p, cb_ctx);
		if (res != 0)
			return res;
	}
	return 0;
}

static void
plugin_load(void *ctx, const char *plugin)
{
	if (strstr(plugin, ".so") == NULL)
		return;

	say_info("Loading plugin: %s", plugin);

	void *dl = dlopen(plugin, RTLD_NOW);
	if (!dl) {
		say_error("Can't load plugin %s: %s", plugin, dlerror());
		return;
	}

	struct tarantool_plugin *p = (typeof(p))dlsym(dl, "plugin_meta");

	if (!p) {
		say_error("Can't find plugin metadata in plugin %s", plugin);
		dlclose(dl);
		return;
	}

	if (p->api_version != PLUGIN_API_VERSION) {
		say_error("Plugin %s has api_version: %d but tarantool has: %d",
			plugin,
			p->api_version,
			PLUGIN_API_VERSION);
		return;
	}

	rlist_add_entry(&loaded_plugins, p, list);

	if (p->init)
		p->init(ctx);

	say_info("Plugin '%s' was loaded, version: %d", p->name, p->version);
}

/** Load all plugins in a plugin dir. */
static void
plugin_dir(struct lua_State *L, const char *dir)
{
	if (!dir)
		return;
	if (!*dir)
		return;
	DIR *dh = opendir(dir);

	if (!dh)
		return;

	struct dirent *dent;
	while ((dent = readdir(dh)) != NULL) {
		if (dent->d_type != DT_REG)
			continue;
		char path[PATH_MAX];
		(void) snprintf(path, sizeof(path), "%s/%s", dir,
				dent->d_name);
		plugin_load(L, path);
	}

	closedir(dh);
}

void
tarantool_plugin_init(struct lua_State *L)
{
       char *plugins = getenv("TARANTOOL_PLUGIN_DIR");

       if (plugins) {
               plugins = strdup(plugins);
               char *ptr = plugins;
               for (;;) {
                       char *divider = strchr(ptr, ':');
                       if (divider == NULL) {
                               plugin_dir(L, ptr);
                               break;
                       }
                       *divider = 0;
                       plugin_dir(L, ptr);
                       ptr = divider + 1;
               }
               free(plugins);
       }
       plugin_dir(L, PLUGIN_DIR);
}
