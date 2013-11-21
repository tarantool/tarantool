#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_pager.h"

extern struct tc tc;

void tc_pager_start() {
	if (tc.pager_pid != 0)
		tc_pager_kill();
	if (tc.opt.pager == NULL) {
		tc.pager_fd = fileno(stdout);
		return;
	}
	int pipefd[2];
	const char *const argv[] = {"/bin/bash", "-c", tc.opt.pager, NULL};

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
