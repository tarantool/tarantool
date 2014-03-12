#ifndef TC_MAIN_H_INCLUDED
#define TC_MAIN_H_INCLUDED
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

#define TC_VERSION_MAJOR "0"
#define TC_VERSION_MINOR "3"

#define TC_DEFAULT_HOST "localhost"
#define TC_DEFAULT_PORT 3301
#define TC_DEFAULT_ADMIN_PORT 3313
#define TC_DEFAULT_HISTORY_FILE ".tarantool_history"

struct tarantool_client {
	struct tbses console;
	struct tc_opt opt;
	int pager_fd;
	pid_t pager_pid;
};

void tc_error(char *fmt, ...);

static inline void
tc_oom(void) {
	tc_error("memory allocation failed");
}

static inline void*
tc_malloc(size_t size) {
	void *p = malloc(size);
	if (p == NULL)
		tc_oom();
	return p;
}

static inline char*
tc_strdup(char *sz) {
	char *p = strdup(sz);
	if (p == NULL)
		tc_oom();
	return p;
}

#define TC_ERRCMD "---\nunknown command. try typing help.\n...\n"

#endif /* TC_H_INCLUDED */
