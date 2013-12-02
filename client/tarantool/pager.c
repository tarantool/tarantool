
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

#include <lib/tarantool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "client/tarantool/opt.h"
#include "client/tarantool/main.h"
#include "client/tarantool/pager.h"

extern struct tarantool_client tc;

void tc_pager_start() {
	if (tc.pager_pid != 0)
		tc_pager_kill();
	if (tc.opt.pager == NULL) {
		tc.pager_fd = fileno(stdout);
		return;
	}
	int pipefd[2];
	const char *const argv[] = {"/bin/sh", "-c", tc.opt.pager, NULL};

	if (pipe(pipefd) < 0)
		tc_error("Failed to open pipe. Errno: %s", strerror(errno));
	pid_t pid = fork();
	if (pid < 0) {
		tc_error("Failed to fork. Errno: %s", strerror(errno));
	} else if (pid == 0) {
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		execve(argv[0], (char * const*)argv, (char * const*)tc.opt.envp);
		tc_error("Can't start pager! Errno: %s", strerror(errno));
	} else {
		close(pipefd[0]);
		tc.pager_fd = pipefd[1];
		tc.pager_pid = pid;
	}
	return;
}

void tc_pager_stop () {
	if (tc.pager_pid != 0) {
		close(tc.pager_fd);
		tc.pager_fd = fileno(stdout);
		waitpid(tc.pager_pid, NULL, 0);
		tc.pager_pid = 0;
	}
	return;
}

void tc_pager_kill () {
	if (tc.pager_pid != 0) {
		kill(tc.pager_pid, SIGTERM);
		tc_pager_stop();
	}
}
