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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#if defined(__APPLE__)
	#include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
  #include <sys/sysctl.h>
#endif

const char *
find_path(const char *argv0)
{
	static char path[PATH_MAX] = {'\0'};
	static bool found = false;

	if (found)
		return path;

	char buf[PATH_MAX];
	size_t size = PATH_MAX - 1;
	if (argv0[0] == '/')
		snprintf(buf, size, "%s", argv0);
	else {
		int rc = -1;
#if defined(__linux__)
		rc = readlink("/proc/self/exe", buf, size);
		if (rc >= 0) {
			/* readlink() does not add a trailing zero */
			buf[rc] = '\0';
		}
#elif defined(__FreeBSD__)
		int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		rc = sysctl(mib, 4, buf, &size, NULL, 0);
#elif defined(__sun)
		snprintf(buf, size, "%s", getexecname());
		rc = 0;
#elif defined(__APPLE__)
		uint32_t usize = size;
		rc = _NSGetExecutablePath(buf, &usize);
#endif
		if (rc == -1)
			snprintf(buf, sizeof(buf) - 1, "%s", getenv("_"));
	}
	if (realpath(buf, path) == NULL)
		snprintf(path, sizeof(path), "%s", buf);
	found = true;
	return path;
}
