/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "chain_exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <yaml.h>
#include "say.h"

#define CONF_FILENAME "tarantool.yaml"
#if defined(__FreeBSD__) || defined(__APPLE__)
    #define GLOBAL_CONF_PATH "/usr/local/etc/tarantool"
#else /* Linux, NetBSD and others */
    #define GLOBAL_CONF_PATH "/etc/tarantool"
#endif

static inline void
joinpath(char *dst, const char *const path, const char *const src)
{
    snprintf(dst, sizeof(char) * (strlen(path) + strlen(src) + 2), \
        (path[strlen(path) - 1] == '/') ? "%s%s" : "%s/%s", path, src);
}

static inline bool
is_dir(const char *const filename)
{
    struct stat sb;
    stat(filename, &sb);
    return S_ISDIR(sb.st_mode);
}

static inline bool
is_dir_contains_file(DIR *dh, const char *const filename)
{
    struct dirent *entry;

    while ((entry = readdir(dh)) != NULL) {
        if (!strcmp(entry->d_name, filename)) {
            return true;
        }
    }

    return false;
}

static inline int
get_absolute_bin_dir_path(char **abspath, const char *const bin_dir, const char *const conf_dir)
{
    /* Path can be relative, in which case we will get
     * an error at the first time realpath call */

    if ((*abspath = realpath(bin_dir, NULL)) == NULL) {
        if ((*abspath = malloc(sizeof(char) * (strlen(bin_dir) + strlen(conf_dir) + 2))) == NULL) {
            say_error("chain_exec: failed to allocate memory");
            return 1;
        }

        /* conf_dir is always absolute */
        joinpath(*abspath, conf_dir, bin_dir);
    }

    return 0;
}

static int
get_bin_dir_from_yaml(char **tarantool_bindir, const char *const conf_dir)
{
    char filename[PATH_MAX];
    joinpath(filename, conf_dir, CONF_FILENAME);

    FILE *conf_file = fopen(filename, "r");
    if (conf_file == NULL) {
        goto out;
    }

    yaml_parser_t parser;
    yaml_token_t token;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, conf_file);

    int scanner_state = 0;
    bool match = false;

    do {
        yaml_parser_scan(&parser, &token);

        switch (token.type) {
            case YAML_KEY_TOKEN:
                scanner_state = 0;
                break;
            case YAML_VALUE_TOKEN:
                scanner_state = 1;
                break;
            case YAML_SCALAR_TOKEN:
                if (scanner_state == 0) {
                    match = !strcmp((char *)token.data.scalar.value, "bin_dir");
                } else if (match) {
                    *tarantool_bindir = strdup((char *)token.data.scalar.value);
                    break;
                }
            default:
                break;
        }
    } while (token.type != YAML_STREAM_END_TOKEN);

    fclose(conf_file);
    yaml_token_delete(&token);
    yaml_parser_delete(&parser);

out:
    return 0;
}

static int
find_conf_dir(char **conf_dir)
{
    /* Search rules: we search in the current directory and on
     * each failure we go down one directory until we find the
     * configuration file or reach root directory. If nothing is
     * found, then we take the standard global configuration file */

    char temp[PATH_MAX];
    char *cur_search_dir = realpath(".", NULL);
    DIR *dh = opendir(cur_search_dir);

    do {
        if (is_dir_contains_file(dh, CONF_FILENAME)) {
            *conf_dir = cur_search_dir;
            return 0;
        }

        joinpath(temp, cur_search_dir, "..");
        free(cur_search_dir);
        if ((cur_search_dir = realpath(temp, NULL)) == NULL) {
            return 1;
        }

        closedir(dh);
        dh = opendir(cur_search_dir);
    } while (dh != NULL && strcmp(cur_search_dir, "/"));

    free(cur_search_dir);

    if ((*conf_dir = malloc(sizeof(char) * (strlen(GLOBAL_CONF_PATH) + 1))) == NULL) {
        return 1;
    }

    strcpy(*conf_dir, GLOBAL_CONF_PATH);
    return 0;
}

int
chain_exec(char *argv[])
{
    int rc = 0;

    char *conf_dir = NULL;
    char *tarantool_bindir = NULL;
    char *tarantool_abs_bindir = NULL;

    if ((rc = find_conf_dir(&conf_dir)) != 0) {
        goto out;
    }

    /* If the specified configutration file is not available,
     we do not consider it an error and continue with usual start */
    if ((rc = get_bin_dir_from_yaml(&tarantool_bindir, conf_dir)) != 0) {
        goto out;
    }

    /* If there is no bin_dir field in the configuration file or it
     * is empty, we continue with usual start */
    if (tarantool_bindir == NULL) {
        goto out;
    }

    if ((rc = get_absolute_bin_dir_path(&tarantool_abs_bindir, tarantool_bindir, conf_dir)) != 0) {
        goto out;
    }

    /* If the path bindir doesn't exist, we don't consider it a error. */
    struct stat sb;
    if (stat(tarantool_abs_bindir, &sb) != 0) {
        goto out;
    }

    if (!is_dir(tarantool_abs_bindir)) {
        say_error("chain_exec: tarantool bin_din %s is not a directory", tarantool_bindir);
        rc = 1;
        goto out;
    }

    char tarantool_bin[PATH_MAX];
    joinpath(tarantool_bin, tarantool_abs_bindir, "tarantool");

    /* This should save us from exec looping */
    if (strcmp(argv[0], tarantool_bin)) {
        argv[0] = tarantool_bin;

        if ((rc = execv(tarantool_bin, argv)) != 0) {
            say_error("chain_exec: failed to exec");
        }
    }

out:
    free(tarantool_abs_bindir);
    free(tarantool_bindir);
    free(conf_dir);
    return rc;
}
